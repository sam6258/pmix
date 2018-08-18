/*
 * Copyright (c) 2015-2018 Intel, Inc. All rights reserved.
 * Copyright (c) 2016      IBM Corporation.  All rights reserved.
 * Copyright (c) 2016-2018 Mellanox Technologies, Inc.
 *                         All rights reserved.
 * Copyright (c) 2018      Research Organization for Information Science
 *                         and Technology (RIST).  All rights reserved.
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
#include "src/util/error.h"
#include "src/mca/gds/base/base.h"

#include "src/mca/common/dstore/dstore_common.h"
#include "ds12_base.h"
#include "ds12_lock.h"
#include "src/mca/common/dstore/dstore_base.h"

static pmix_common_dstore_ctx_t *ds12_ctx;

static pmix_status_t ds12_init(pmix_info_t info[], size_t ninfo)
{
    pmix_status_t rc = PMIX_SUCCESS;

    ds12_ctx = pmix_common_dstor_init(&pmix_ds12_lock_module, "ds12", info, ninfo);
    if (NULL == ds12_ctx) {
        rc = PMIX_ERR_INIT;
    }

    return rc;
}

static void ds12_finalize(void)
{
    pmix_common_dstor_finalize(ds12_ctx);
}

static pmix_status_t ds12_assign_module(pmix_info_t *info, size_t ninfo,
                                        int *priority)
{
    return pmix_common_dstor_assign_module(ds12_ctx, info, ninfo, priority);
}

static pmix_status_t ds12_cache_job_info(struct pmix_nspace_t *ns,
                                pmix_info_t info[], size_t ninfo)
{
    return PMIX_SUCCESS;
}

static pmix_status_t ds12_register_job_info(struct pmix_peer_t *pr,
                                            pmix_buffer_t *reply)
{
    return pmix_common_dstor_register_job_info(ds12_ctx, pr, reply);
}

static pmix_status_t ds12_store_job_info(const char *nspace,  pmix_buffer_t *buf)
{
    return pmix_common_dstor_store_job_info(ds12_ctx, nspace, buf);
}

static pmix_status_t ds12_store(const pmix_proc_t *proc,
                                    pmix_scope_t scope,
                                    pmix_kval_t *kv)
{
    return pmix_common_dstor_store(ds12_ctx, proc, scope, kv);
}

/* this function is only called by the PMIx server when its
 * host has received data from some other peer. It therefore
 * always contains data solely from remote procs, and we
 * shall store it accordingly */
static pmix_status_t ds12_store_modex(struct pmix_nspace_t *nspace,
                                      pmix_list_t *cbs,
                                      pmix_byte_object_t *bo)
{
    return pmix_common_dstor_store_modex(ds12_ctx, nspace, cbs, bo);
}

static pmix_status_t ds12_fetch(const pmix_proc_t *proc,
                                    pmix_scope_t scope, bool copy,
                                    const char *key,
                                    pmix_info_t info[], size_t ninfo,
                                    pmix_list_t *kvs)
{
    return pmix_common_dstor_fetch(ds12_ctx, proc, scope, copy, key, info, ninfo, kvs);
}

static pmix_status_t ds12_setup_fork(const pmix_proc_t *peer, char ***env)
{
    return pmix_common_dstor_setup_fork(ds12_ctx, PMIX_DSTORE_ESH_BASE_PATH, peer, env);
}

static pmix_status_t ds12_add_nspace(const char *nspace,
                                pmix_info_t info[],
                                size_t ninfo)
{
    return pmix_common_dstor_add_nspace(ds12_ctx, nspace, info, ninfo);
}

static pmix_status_t ds12_del_nspace(const char* nspace)
{
    return pmix_common_dstor_del_nspace(ds12_ctx, nspace);
}

pmix_gds_base_module_t pmix_ds12_module = {
    .name = "ds12",
    .init = ds12_init,
    .finalize = ds12_finalize,
    .assign_module = ds12_assign_module,
    .cache_job_info = ds12_cache_job_info,
    .register_job_info = ds12_register_job_info,
    .store_job_info = ds12_store_job_info,
    .store = ds12_store,
    .store_modex = ds12_store_modex,
    .fetch = ds12_fetch,
    .setup_fork = ds12_setup_fork,
    .add_nspace = ds12_add_nspace,
    .del_nspace = ds12_del_nspace,
};

