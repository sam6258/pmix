/*
 * Copyright (c) 2015-2017 Intel, Inc. All rights reserved.
 * Copyright (c) 2017      Mellanox Technologies, Inc.
 *                         All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#ifndef PMIX_DSTORE_H
#define PMIX_DSTORE_H

#include <src/include/pmix_config.h>


#include "src/mca/gds/gds.h"
#include "src/mca/pshmem/pshmem.h"

BEGIN_C_DECLS

#include <src/include/pmix_config.h>
#include "src/class/pmix_value_array.h"
#include "dstore_common.h"

#define INITIAL_SEG_SIZE 4096
#define NS_META_SEG_SIZE (1<<22)
#define NS_DATA_SEG_SIZE (1<<22)

#define PMIX_DSTORE_ESH_BASE_PATH "PMIX_DSTORE_ESH_BASE_PATH"
#define PMIX_DSTORE_VER_BASE_PATH_FMT "PMIX_DSTORE_%d_BASE_PATH"

typedef struct ns_map_data_s ns_map_data_t;
typedef struct session_s session_t;
typedef struct ns_map_s ns_map_t;
typedef struct seg_desc_t seg_desc_t;

typedef ns_map_data_t * (*session_map_search_fn_t)(pmix_common_dstore_ctx_t *ds_ctx,
                                                   const char *nspace);

struct pmix_common_dstore_ctx_s {
    char *ds_name;
    char *base_path;
    uid_t jobuid;
    char setjobuid;

    pmix_value_array_t *session_array;
    pmix_value_array_t *ns_map_array;
    pmix_value_array_t *ns_track_array;

    pmix_common_lock_callbacks_t *lock_cbs;

    size_t initial_segment_size;
    size_t max_ns_num;
    size_t meta_segment_size;
    size_t max_meta_elems;
    size_t data_segment_size;
    size_t lock_segment_size;
    int direct_mode;
    session_map_search_fn_t session_map_search;
    pmix_peer_t *clients_peer;
};

/* this structs are used to store information about
 * shared segments addresses locally at each process,
 * so they are common for different types of segments
 * and don't have a specific content (namespace's info,
 * rank's meta info, ranks's data). */

typedef enum {
    INITIAL_SEGMENT,
    NS_META_SEGMENT,
    NS_DATA_SEGMENT
} segment_type;

struct seg_desc_t {
    segment_type type;
    pmix_pshmem_seg_t seg_info;
    uint32_t id;
    seg_desc_t *next;
};

struct session_s {
    int in_use;
    uid_t jobuid;
    char setjobuid;
    char *nspace_path;
    seg_desc_t *sm_seg_first;
    seg_desc_t *sm_seg_last;
};

struct ns_map_data_s {
    char name[PMIX_MAX_NSLEN+1];
    size_t tbl_idx;
    int track_idx;
    pmix_common_dstor_lock_ctx_t lock;
};

struct ns_map_s {
    int in_use;
    ns_map_data_t data;
};

/* initial segment format:
 * size_t num_elems;
 * size_t full; //indicate to client that it needs to attach to the next segment
 * ns_seg_info_t ns_seg_info[max_ns_num];
 */

typedef struct {
    ns_map_data_t ns_map;
    size_t num_meta_seg;/* read by clients to attach to this number of segments. */
    size_t num_data_seg;
} ns_seg_info_t;

/* meta segment format:
 * size_t num_elems;
 * rank_meta_info meta_info[max_meta_elems];
 */

typedef struct {
    size_t rank;
    size_t offset;
    size_t count;
} rank_meta_info;

typedef struct {
    pmix_value_array_t super;
    ns_map_data_t ns_map;
    size_t num_meta_seg;
    size_t num_data_seg;
    seg_desc_t *meta_seg;
    seg_desc_t *data_seg;
    bool in_use;
} ns_track_elem_t;

END_C_DECLS

#endif
