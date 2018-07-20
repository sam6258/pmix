/*
 * Copyright (c) 2018      Mellanox Technologies, Inc.
 *                         All rights reserved.
 *
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include <src/include/pmix_config.h>
#include <pmix_common.h>

#include <stdio.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#include "src/mca/common/dstore/dstore_common.h"
#include "src/mca/gds/base/base.h"
#include "src/mca/pshmem/pshmem.h"
#include "src/class/pmix_list.h"

#include "src/util/error.h"
#include "src/util/output.h"

#include "gds_ds21_lock.h"
#include "src/mca/common/dstore/dstore_segment.h"

typedef struct {
    pmix_list_item_t super;

    char *lockfile;
    pmix_dstore_seg_desc_t *seg_desc;
    /*
     * Lock segment format:
     * 1. Segment size             sizeof(size_t)
     * 2. local_size:              sizeof(uint32_t)
     * 3. Array of in use indexes: sizeof(int32_t)*local_size
     * 4. Double array of locks:   sizeof(pthread_mutex_t)*local_size*2
     */
    pthread_mutex_t *mutex;
    uint32_t num_locks;
    uint32_t lock_idx;
} lock_item_t;

typedef struct {
    pmix_list_t lock_traker;
} lock_ctx_t;

typedef pmix_list_t ds21_lock_pthread_ctx_t;

typedef struct {
   size_t   seg_size;
   uint32_t num_locks;
} segment_hdr_t;

#define _GET_IDX_PTR(seg_ptr) \
    ((int32_t*)(seg_ptr + sizeof(uint32_t)*3 + sizeof(size_t)))

#define _GET_MUTEX_PTR(seg_ptr, idx_num) \
    ((pthread_mutex_t*)(seg_ptr + sizeof(uint32_t)*3 + sizeof(size_t) + sizeof(int32_t) * idx_num))

static void ncon(lock_item_t *p) {
    p->lockfile = NULL;
    p->lock_idx = 0;
    p->mutex = NULL;
    p->num_locks = 0;
    p->seg_desc = NULL;
}

static void ldes(lock_item_t *p) {
    uint32_t i;

    if(PMIX_PROC_IS_SERVER(pmix_globals.mypeer)) {
        if (p->lockfile) {
            unlink(p->lockfile);
        }
        for(i = 0; i < p->num_locks * 2; i++) {
            if (0 != pthread_mutex_destroy(&p->mutex[i])) {
                PMIX_ERROR_LOG(PMIX_ERROR);
            }
        }
    }
    if (p->lockfile) {
        free(p->lockfile);
    }
    if (p->seg_desc) {
        pmix_common_dstor_delete_sm_desc(p->seg_desc);
    }
}

PMIX_CLASS_INSTANCE(lock_item_t,
                    pmix_list_item_t,
                    ncon, ldes);

pmix_status_t pmix_gds_ds21_lock_init(pmix_common_dstor_lock_ctx_t *ctx, const char *base_path, const char * name,
                                      uint32_t local_size, uid_t uid, bool setuid)
{
    pthread_mutexattr_t attr;
    size_t size;
    uint32_t i;
    int page_size = pmix_common_dstor_getpagesize();
    segment_hdr_t *seg_hdr;
    lock_item_t *lock_item = NULL;
    lock_ctx_t *lock_ctx = (lock_ctx_t*)*ctx;
    pmix_list_t *lock_tracker;
    pmix_status_t rc = PMIX_SUCCESS;

    if (NULL == *ctx) {
        lock_ctx = (lock_ctx_t*)malloc(sizeof(lock_ctx_t));
        if (NULL == lock_ctx) {
            rc = PMIX_ERR_INIT;
            PMIX_ERROR_LOG(rc);
            goto error;
        }
        memset(lock_ctx, 0, sizeof(lock_ctx_t));
        PMIX_CONSTRUCT(&lock_ctx->lock_traker, pmix_list_t);
        *ctx = lock_ctx;
    }

    lock_tracker = &lock_ctx->lock_traker;
    lock_item = PMIX_NEW(lock_item_t);

    if (NULL == lock_item) {
        rc = PMIX_ERR_INIT;
        PMIX_ERROR_LOG(rc);
        goto error;
    }
    pmix_list_append(lock_tracker, &lock_item->super);

    PMIX_OUTPUT_VERBOSE((10, pmix_gds_base_framework.framework_output,
        "%s:%d:%s local_size %d", __FILE__, __LINE__, __func__, local_size));

    if (PMIX_PROC_IS_SERVER(pmix_globals.mypeer)) {
        size = ((sizeof(uint32_t) + /* local_size */
                sizeof(int32_t) * local_size + /* array of inuse flag */
                2 * local_size * sizeof(pthread_mutex_t)) /* array of mutexes */
                / page_size + 1) * page_size;

        lock_item->seg_desc = pmix_common_dstor_create_new_lock_seg(base_path,
                                    size, name, 0, uid, setuid);
        if (NULL == lock_item->seg_desc) {
            rc = PMIX_ERR_OUT_OF_RESOURCE;
            PMIX_ERROR_LOG(rc);
            goto error;
        }

        if (0 != pthread_mutexattr_init(&attr)) {
            rc = PMIX_ERR_INIT;
            PMIX_ERROR_LOG(rc);
            goto error;
        }
        if (0 != pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED)) {
            pthread_mutexattr_destroy(&attr);
            rc = PMIX_ERR_INIT;
            PMIX_ERROR_LOG(rc);
            goto error;
        }

        lock_item->lockfile = strdup(lock_item->seg_desc->seg_info.seg_name);
        lock_item->num_locks = local_size;
        lock_item->mutex = _GET_MUTEX_PTR(lock_item->seg_desc->seg_info.seg_base_addr,
                                         local_size);
        segment_hdr_t *seg_hdr = (segment_hdr_t*)lock_item->seg_desc->seg_info.seg_base_addr;
        seg_hdr->num_locks = local_size;
        seg_hdr->seg_size = size;

        for(i = 0; i < local_size * 2; i++) {
            if (0 != pthread_mutex_init(&lock_item->mutex[i], &attr)) {
                pthread_mutexattr_destroy(&attr);
                rc = PMIX_ERR_INIT;
                PMIX_ERROR_LOG(rc);
                goto error;
            }
        }
        if (0 != pthread_mutexattr_destroy(&attr)) {
            rc = PMIX_ERR_INIT;
            PMIX_ERROR_LOG(PMIX_ERR_INIT);
            goto error;
        }
    }
    else {
        int32_t *lock_idx_ptr;
        bool idx_found = false;

        size = pmix_common_dstor_getpagesize();
        lock_item->seg_desc = pmix_common_dstor_attach_new_lock_seg(base_path, size, name, 0);
        if (NULL == lock_item->seg_desc) {
            rc = PMIX_ERR_OUT_OF_RESOURCE;
            PMIX_ERROR_LOG(rc);
            goto error;
        }
        seg_hdr = (segment_hdr_t*)lock_item->seg_desc->seg_info.seg_base_addr;
        lock_item->num_locks = seg_hdr->num_locks;
        lock_idx_ptr = _GET_IDX_PTR(lock_item->seg_desc->seg_info.seg_base_addr);

        for (i = 0; i < lock_item->num_locks; i++) {
            int32_t expected = 0;
            if (pmix_atomic_compare_exchange_strong_32(&lock_idx_ptr[i], &expected, 1)) {
                lock_item->lock_idx = i;
                lock_item->mutex = _GET_MUTEX_PTR(lock_item->seg_desc->seg_info.seg_base_addr,
                                                 lock_item->num_locks);
                lock_item->lockfile = strdup(lock_item->seg_desc->seg_info.seg_name);
                idx_found = true;
                break;
            }
        }

        if (false == idx_found) {
            rc = PMIX_ERR_NOT_FOUND;
            PMIX_ERROR_LOG(rc);
            goto error;
        }
    }

    return rc;

error:
    if (NULL != lock_item) {
        pmix_list_remove_item(lock_tracker, &lock_item->super);
        PMIX_RELEASE(lock_item);
        lock_item = NULL;
    }
    *ctx = NULL;

    return rc;
}

void pmix_ds21_lock_finalize(pmix_common_dstor_lock_ctx_t *lock_ctx)
{
    lock_item_t *lock_item, *item_next;
    pmix_list_t *lock_tracker = &((lock_ctx_t*)*lock_ctx)->lock_traker;

    if (NULL == lock_tracker) {
        PMIX_ERROR_LOG(PMIX_ERR_NOT_FOUND);
        return;
    }

    PMIX_LIST_FOREACH_SAFE(lock_item, item_next, lock_tracker, lock_item_t) {
        pmix_list_remove_item(lock_tracker, &lock_item->super);
        PMIX_RELEASE(lock_item);
    }
    if (pmix_list_is_empty(lock_tracker)) {
        PMIX_LIST_DESTRUCT(lock_tracker);
        free(lock_tracker);
        lock_tracker = NULL;
    }
    *lock_ctx = NULL;
}

pmix_status_t pmix_ds21_lock_wr_get(pmix_common_dstor_lock_ctx_t lock_ctx)
{
    lock_item_t *lock_item;
    pmix_list_t *lock_tracker = &((lock_ctx_t*)lock_ctx)->lock_traker;
    pthread_mutex_t *locks;
    uint32_t num_locks;
    uint32_t i;
    pmix_status_t rc;

    if (NULL == lock_tracker) {
        rc = PMIX_ERR_NOT_FOUND;
        PMIX_ERROR_LOG(PMIX_ERR_NOT_FOUND);
        return rc;
    }

    PMIX_LIST_FOREACH(lock_item, lock_tracker, lock_item_t) {
        locks = lock_item->mutex;
        num_locks = lock_item->num_locks;

         /* Lock the "signalling" lock first to let clients know that
         * server is going to get a write lock.
         * Clients do not hold this lock for a long time,
         * so this loop should be relatively dast.
         */
        for (i = 0; i < num_locks; i++) {
            if (0 != pthread_mutex_lock(&locks[2*i])) {
                return PMIX_ERROR;
            }
        }

        /* Now we can go and grab the main locks
         * New clients will be stopped at the previous
         * "barrier" locks.
         * We will wait here while all clients currently holding
         * locks will be done
         */
        for(i = 0; i < num_locks; i++) {
            if (0 != pthread_mutex_lock(&locks[2*i + 1])) {
                return PMIX_ERROR;
            }
        }
    }
    return PMIX_SUCCESS;
}

pmix_status_t pmix_ds21_lock_wr_rel(pmix_common_dstor_lock_ctx_t lock_ctx)
{
    lock_item_t *lock_item;
    pmix_list_t *lock_tracker = &((lock_ctx_t*)lock_ctx)->lock_traker;
    pthread_mutex_t *locks;
    uint32_t num_locks;
    uint32_t i;
    pmix_status_t rc;

    if (NULL == lock_tracker) {
        rc = PMIX_ERR_NOT_FOUND;
        PMIX_ERROR_LOG(rc);
        return rc;
    }

    PMIX_LIST_FOREACH(lock_item, lock_tracker, lock_item_t) {
        locks = lock_item->mutex;
        num_locks = lock_item->num_locks;

        /* Lock the second lock first to ensure that all procs will see
         * that we are trying to grab the main one */
        for(i=0; i<num_locks; i++) {
            if (0 != pthread_mutex_unlock(&locks[2*i])) {
                return PMIX_ERROR;
            }
            if (0 != pthread_mutex_unlock(&locks[2*i + 1])) {
                return PMIX_ERROR;
            }
        }
    }

    return PMIX_SUCCESS;
}

pmix_status_t pmix_ds21_lock_rd_get(pmix_common_dstor_lock_ctx_t lock_ctx)
{
    lock_item_t *lock_item;
    pmix_list_t *lock_tracker = &((lock_ctx_t*)lock_ctx)->lock_traker;
    pthread_mutex_t *locks;
    uint32_t idx;
    pmix_status_t rc;

    if (NULL == lock_tracker) {
        rc = PMIX_ERR_NOT_FOUND;
        PMIX_ERROR_LOG(rc);
        return rc;
    }

    lock_item = (lock_item_t*)pmix_list_get_first(lock_tracker);

    locks = lock_item->mutex;
    idx = lock_item->lock_idx;

    /* This mutex is only used to acquire the next one,
     * this is a barrier that server is using to let clients
     * know that it is going to grab the write lock
     */
    if (0 != pthread_mutex_lock(&locks[2 * idx])) {
        return PMIX_ERROR;
    }

    /* Now grab the main lock */
    if (0 != pthread_mutex_lock(&locks[2*idx + 1])) {
        return PMIX_ERROR;
    }

    /* Once done - release signalling lock */
    if (0 != pthread_mutex_unlock(&locks[2*idx])) {
        return PMIX_ERROR;
    }

    return PMIX_SUCCESS;
}

pmix_status_t pmix_ds21_lock_rd_rel(pmix_common_dstor_lock_ctx_t lock_ctx)
{
    lock_item_t *lock_item;
    pmix_list_t *lock_tracker = &((lock_ctx_t*)lock_ctx)->lock_traker;
    pmix_status_t rc;
    pthread_mutex_t *locks;
    uint32_t idx;

    if (NULL == lock_tracker) {
        rc = PMIX_ERR_NOT_FOUND;
        PMIX_ERROR_LOG(rc);
        return rc;
    }

    lock_item = (lock_item_t*)pmix_list_get_first(lock_tracker);

    locks = lock_item->mutex;
    idx = lock_item->lock_idx;

    /* Release the main lock */
    if (0 != pthread_mutex_unlock(&locks[2*idx + 1])) {
        return PMIX_SUCCESS;
    }

    return PMIX_SUCCESS;
}
