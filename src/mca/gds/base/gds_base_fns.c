/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2015-2017 Intel, Inc. All rights reserved.
 * Copyright (c) 2016      Mellanox Technologies, Inc.
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
#include "src/include/pmix_globals.h"

#include "src/class/pmix_list.h"
#include "src/util/argv.h"
#include "src/util/error.h"

#include "src/mca/gds/base/base.h"


char* pmix_gds_base_get_available_modules(void)
{
    if (!pmix_gds_globals.initialized) {
        return NULL;
    }

    return strdup(pmix_gds_globals.all_mods);
}

/* Select a gds module per the given directives */
pmix_gds_base_module_t* pmix_gds_base_assign_module(pmix_info_t *info, size_t ninfo)
{
    pmix_gds_base_active_module_t *active;
    pmix_gds_base_module_t *mod = NULL;
    int pri, priority = -1;

    if (!pmix_gds_globals.initialized) {
        return NULL;
    }

    PMIX_LIST_FOREACH(active, &pmix_gds_globals.actives, pmix_gds_base_active_module_t) {
        if (NULL == active->module->assign_module) {
            continue;
        }
        if (PMIX_SUCCESS == active->module->assign_module(info, ninfo, &pri)) {
            if (pri < 0) {
                /* use the default priority from the component */
                pri = active->pri;
            }
            if (priority < pri) {
                mod = active->module;
                priority = pri;
            }
        }
    }

    return mod;
}

pmix_status_t pmix_gds_base_setup_fork(const pmix_proc_t *proc,
                                       char ***env)
{
    pmix_gds_base_active_module_t *active;
    pmix_status_t rc;

    if (!pmix_gds_globals.initialized) {
        return PMIX_ERR_INIT;
    }

    PMIX_LIST_FOREACH(active, &pmix_gds_globals.actives, pmix_gds_base_active_module_t) {
        if (NULL == active->module->setup_fork) {
            continue;
        }
        if (PMIX_SUCCESS != (rc = active->module->setup_fork(proc, env))) {
            return rc;
        }
    }

    return PMIX_SUCCESS;
}


pmix_status_t pmix_gds_base_store_modex(struct pmix_namespace_t *nspace,
        pmix_list_t *cbs,
        pmix_buffer_t * buff,
        pmix_gds_base_store_modex_cb_fn_t cb_fn,
        pmix_gds_base_store_modex_cbdata_t cbdata)
{
    pmix_status_t rc = PMIX_SUCCESS;
    pmix_namespace_t * ns = (struct pmix_namespace_t *)nspace;
    pmix_server_caddy_t *cd;
    pmix_buffer_t bkt;
    pmix_byte_object_t bo, bo2;
    int32_t cnt = 1;
    char byte;
    pmix_collect_t ctype;
    bool holding_ns_lock = false;

    // Lock down all of the namespaces
    PMIX_GDS_ACQUIRE_NS_LOCK(rc, ns);
    if (PMIX_SUCCESS != rc) {
        PMIX_ERROR_LOG(rc);
        goto error;
    }

    holding_ns_lock = true;

    /* Loop over the enclosed byte object envelopes and
     * store them in our GDS module */
    cnt = 1;
    PMIX_BFROPS_UNPACK(rc, pmix_globals.mypeer,
            buff, &bo, &cnt, PMIX_BYTE_OBJECT);
    while (PMIX_SUCCESS == rc) {
        PMIX_LOAD_BUFFER(pmix_globals.mypeer, &bkt, bo.bytes, bo.size);
        /* unpack the data collection flag */
        cnt = 1;
        PMIX_BFROPS_UNPACK(rc, pmix_globals.mypeer,
                &bkt, &byte, &cnt, PMIX_BYTE);
        if (PMIX_ERR_UNPACK_READ_PAST_END_OF_BUFFER == rc) {
            /* no data was returned, so we are done with this blob */
            break;
        }
        if (PMIX_SUCCESS != rc) {
            /* we have an error */
            break;
        }

        // Check that this blob was accumulated with the same data collection setting
        /*
        ctype = (pmix_collect_t)byte;
        if (ctype != cd->trk->collect_type) {
            rc = PMIX_ERR_INVALID_ARG;
            break;
        }
        */

        /* unpack the enclosed blobs from the various peers */
        cnt = 1;
        PMIX_BFROPS_UNPACK(rc, pmix_globals.mypeer,
                &bkt, &bo2, &cnt, PMIX_BYTE_OBJECT);
        while (PMIX_SUCCESS == rc) {
            /* unpack all the kval's from this peer and store them in
             * our GDS. Note that PMIx by design holds all data at
             * the server level until requested. If our GDS is a
             * shared memory region, then the data may be available
             * right away - but the client still has to be notified
             * of its presence. */
            rc = cb_fn(cbdata, ns, cbs, &bo2);
            if (PMIX_SUCCESS != rc) {
                PMIX_ERROR_LOG(rc);
                break;
            }
            PMIX_BYTE_OBJECT_DESTRUCT(&bo2);
            /* get the next blob */
            cnt = 1;
            PMIX_BFROPS_UNPACK(rc, pmix_globals.mypeer,
                    &bkt, &bo2, &cnt, PMIX_BYTE_OBJECT);
        }
        if (PMIX_ERR_UNPACK_READ_PAST_END_OF_BUFFER == rc) {
            rc = PMIX_SUCCESS;
        } else if (PMIX_SUCCESS != rc) {
            PMIX_ERROR_LOG(rc);
            goto error;
        }
        /* unpack and process the next blob */
        cnt = 1;
        PMIX_BFROPS_UNPACK(rc, pmix_globals.mypeer,
                buff, &bo, &cnt, PMIX_BYTE_OBJECT);
    }
    if (PMIX_ERR_UNPACK_READ_PAST_END_OF_BUFFER == rc) {
        rc = PMIX_SUCCESS;
    }
error:

    // Unlock all of the namespaces
    if( holding_ns_lock ) {
        PMIX_GDS_RELEASE_NS_LOCK(rc, ns);
        if (PMIX_SUCCESS != rc) {
            PMIX_ERROR_LOG(rc);
        }    
    }  
    if (PMIX_SUCCESS != rc) {
        PMIX_ERROR_LOG(rc);
    }    

    return rc;
}
