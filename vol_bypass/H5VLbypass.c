/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5.  The full HDF5 copyright notice, including     *
 * terms governing use, modification, and redistribution, is contained in    *
 * the COPYING file, which can be found at the root of the source code       *
 * distribution tree, or in https://support.hdfgroup.org/ftp/HDF5/releases.  *
 * If you do not have access to either file, you may request a copy from     *
 * help@hdfgroup.org.                                                        *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*
 * Purpose:     This is a "bypass" VOL connector, which forwards each
 *              VOL callback to an underlying connector.
 *
 *              It is designed as an example VOL connector for developers to
 *              use when creating new connectors, especially connectors that
 *              are outside of the HDF5 library.  As such, it should _NOT_
 *              include _any_ private HDF5 header files.  This connector should
 *              therefore only make public HDF5 API calls and use standard C /
 *              POSIX calls.
 *
 *              Note that the HDF5 error stack must be preserved on code paths
 *              that could be invoked when the underlying VOL connector's
 *              callback can fail.
 *
 */


/* Header files needed */
/* Do NOT include private HDF5 files here! */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <errno.h>
#include <fcntl.h>

/* Public HDF5 headers */
#include "hdf5.h"

/* This connector's private header */
#include "H5VLbypass_private.h"

extern int errno;

/**********/
/* Macros */
/**********/

/* Whether to display log messge when callback is invoked */
/* (Uncomment to enable) */
/* #define ENABLE_BYPASS_LOGGING */

/************/
/* Typedefs */
/************/

/* The bypass VOL connector's object */
typedef struct H5VL_bypass_t {
    hid_t  under_vol_id;        /* ID for underlying VOL connector */
    void   *under_object;       /* Underlying VOL connector's object */
} H5VL_bypass_t;

/* The bypass VOL wrapper context */
typedef struct H5VL_bypass_wrap_ctx_t {
    hid_t under_vol_id;         /* VOL ID for under VOL */
    void *under_wrap_ctx;       /* Object wrapping context for under VOL */
} H5VL_bypass_wrap_ctx_t;

/********************* */
/* Function prototypes */
/********************* */

/* Helper routines */
static H5VL_bypass_t *H5VL_bypass_new_obj(void *under_obj,
    hid_t under_vol_id);
static herr_t H5VL_bypass_free_obj(H5VL_bypass_t *obj);

/* "Management" callbacks */
static herr_t H5VL_bypass_init(hid_t vipl_id);
static herr_t H5VL_bypass_term(void);

/* VOL info callbacks */
static void *H5VL_bypass_info_copy(const void *info);
static herr_t H5VL_bypass_info_cmp(int *cmp_value, const void *info1, const void *info2);
static herr_t H5VL_bypass_info_free(void *info);
static herr_t H5VL_bypass_info_to_str(const void *info, char **str);
static herr_t H5VL_bypass_str_to_info(const char *str, void **info);

/* VOL object wrap / retrieval callbacks */
static void *H5VL_bypass_get_object(const void *obj);
static herr_t H5VL_bypass_get_wrap_ctx(const void *obj, void **wrap_ctx);
static void *H5VL_bypass_wrap_object(void *obj, H5I_type_t obj_type,
    void *wrap_ctx);
static void *H5VL_bypass_unwrap_object(void *obj);
static herr_t H5VL_bypass_free_wrap_ctx(void *obj);

/* Attribute callbacks */
static void *H5VL_bypass_attr_create(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t type_id, hid_t space_id, hid_t acpl_id, hid_t aapl_id, hid_t dxpl_id, void **req);
static void *H5VL_bypass_attr_open(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t aapl_id, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_attr_read(void *attr, hid_t mem_type_id, void *buf, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_attr_write(void *attr, hid_t mem_type_id, const void *buf, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_attr_get(void *obj, H5VL_attr_get_args_t *args, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_attr_specific(void *obj, const H5VL_loc_params_t *loc_params, H5VL_attr_specific_args_t *args, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_attr_optional(void *obj, H5VL_optional_args_t *args, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_attr_close(void *attr, hid_t dxpl_id, void **req);

/* Dataset callbacks */
static void *H5VL_bypass_dataset_create(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t lcpl_id, hid_t type_id, hid_t space_id, hid_t dcpl_id, hid_t dapl_id, hid_t dxpl_id, void **req);
static void *H5VL_bypass_dataset_open(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t dapl_id, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_dataset_read(size_t count, void *dset[],
        hid_t mem_type_id[], hid_t mem_space_id[], hid_t file_space_id[],
        hid_t plist_id, void *buf[], void **req);
static herr_t H5VL_bypass_dataset_write(size_t count, void *dset[],
        hid_t mem_type_id[], hid_t mem_space_id[], hid_t file_space_id[],
        hid_t plist_id, const void *buf[], void **req);
static herr_t H5VL_bypass_dataset_get(void *dset, H5VL_dataset_get_args_t *args, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_dataset_specific(void *obj, H5VL_dataset_specific_args_t *args, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_dataset_optional(void *obj, H5VL_optional_args_t *args, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_dataset_close(void *dset, hid_t dxpl_id, void **req);

/* Datatype callbacks */
static void *H5VL_bypass_datatype_commit(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t type_id, hid_t lcpl_id, hid_t tcpl_id, hid_t tapl_id, hid_t dxpl_id, void **req);
static void *H5VL_bypass_datatype_open(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t tapl_id, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_datatype_get(void *dt, H5VL_datatype_get_args_t *args, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_datatype_specific(void *obj, H5VL_datatype_specific_args_t *args, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_datatype_optional(void *obj, H5VL_optional_args_t *args, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_datatype_close(void *dt, hid_t dxpl_id, void **req);

/* File callbacks */
static void *H5VL_bypass_file_create(const char *name, unsigned flags, hid_t fcpl_id, hid_t fapl_id, hid_t dxpl_id, void **req);
static void *H5VL_bypass_file_open(const char *name, unsigned flags, hid_t fapl_id, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_file_get(void *file, H5VL_file_get_args_t *args, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_file_specific(void *file, H5VL_file_specific_args_t *args, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_file_optional(void *file, H5VL_optional_args_t *args, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_file_close(void *file, hid_t dxpl_id, void **req);

/* Group callbacks */
static void *H5VL_bypass_group_create(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t lcpl_id, hid_t gcpl_id, hid_t gapl_id, hid_t dxpl_id, void **req);
static void *H5VL_bypass_group_open(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t gapl_id, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_group_get(void *obj, H5VL_group_get_args_t *args, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_group_specific(void *obj, H5VL_group_specific_args_t *args, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_group_optional(void *obj, H5VL_optional_args_t *args, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_group_close(void *grp, hid_t dxpl_id, void **req);

/* Link callbacks */
static herr_t H5VL_bypass_link_create(H5VL_link_create_args_t *args, void *obj, const H5VL_loc_params_t *loc_params, hid_t lcpl_id, hid_t lapl_id, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_link_copy(void *src_obj, const H5VL_loc_params_t *loc_params1, void *dst_obj, const H5VL_loc_params_t *loc_params2, hid_t lcpl_id, hid_t lapl_id, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_link_move(void *src_obj, const H5VL_loc_params_t *loc_params1, void *dst_obj, const H5VL_loc_params_t *loc_params2, hid_t lcpl_id, hid_t lapl_id, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_link_get(void *obj, const H5VL_loc_params_t *loc_params, H5VL_link_get_args_t *args, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_link_specific(void *obj, const H5VL_loc_params_t *loc_params, H5VL_link_specific_args_t *args, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_link_optional(void *obj, const H5VL_loc_params_t *loc_params, H5VL_optional_args_t *args, hid_t dxpl_id, void **req);

/* Object callbacks */
static void *H5VL_bypass_object_open(void *obj, const H5VL_loc_params_t *loc_params, H5I_type_t *opened_type, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_object_copy(void *src_obj, const H5VL_loc_params_t *src_loc_params, const char *src_name, void *dst_obj, const H5VL_loc_params_t *dst_loc_params, const char *dst_name, hid_t ocpypl_id, hid_t lcpl_id, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_object_get(void *obj, const H5VL_loc_params_t *loc_params, H5VL_object_get_args_t *args, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_object_specific(void *obj, const H5VL_loc_params_t *loc_params, H5VL_object_specific_args_t *args, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_object_optional(void *obj, const H5VL_loc_params_t *loc_params, H5VL_optional_args_t *args, hid_t dxpl_id, void **req);

/* Container/connector introspection callbacks */
static herr_t H5VL_bypass_introspect_get_conn_cls(void *obj, H5VL_get_conn_lvl_t lvl, const H5VL_class_t **conn_cls);
static herr_t H5VL_bypass_introspect_get_cap_flags(const void *info, uint64_t *cap_flags);
static herr_t H5VL_bypass_introspect_opt_query(void *obj, H5VL_subclass_t cls, int op_type, uint64_t *flags);

/* Async request callbacks */
static herr_t H5VL_bypass_request_wait(void *req, uint64_t timeout, H5VL_request_status_t *status);
static herr_t H5VL_bypass_request_notify(void *obj, H5VL_request_notify_t cb, void *ctx);
static herr_t H5VL_bypass_request_cancel(void *req, H5VL_request_status_t *status);
static herr_t H5VL_bypass_request_specific(void *req, H5VL_request_specific_args_t *args);
static herr_t H5VL_bypass_request_optional(void *req, H5VL_optional_args_t *args);
static herr_t H5VL_bypass_request_free(void *req);

/* Blob callbacks */
static herr_t H5VL_bypass_blob_put(void *obj, const void *buf, size_t size, void *blob_id, void *ctx);
static herr_t H5VL_bypass_blob_get(void *obj, const void *blob_id, void *buf, size_t size, void *ctx);
static herr_t H5VL_bypass_blob_specific(void *obj, void *blob_id, H5VL_blob_specific_args_t *args);
static herr_t H5VL_bypass_blob_optional(void *obj, void *blob_id, H5VL_optional_args_t *args);

/* Token callbacks */
static herr_t H5VL_bypass_token_cmp(void *obj, const H5O_token_t *token1, const H5O_token_t *token2, int *cmp_value);
static herr_t H5VL_bypass_token_to_str(void *obj, H5I_type_t obj_type, const H5O_token_t *token, char **token_str);
static herr_t H5VL_bypass_token_from_str(void *obj, H5I_type_t obj_type, const char *token_str, H5O_token_t *token);

/* Generic optional callback */
static herr_t H5VL_bypass_optional(void *obj, H5VL_optional_args_t *args, hid_t dxpl_id, void **req);

/*******************/
/* Local variables */
/*******************/

/* Bypass VOL connector class struct */
static const H5VL_class_t H5VL_bypass_g = {
    H5VL_VERSION,                                       /* VOL class struct version */
    (H5VL_class_value_t)H5VL_BYPASS_VALUE,              /* value        */
    H5VL_BYPASS_NAME,                                   /* name         */
    H5VL_BYPASS_VERSION,                                /* connector version */
    0,                                                  /* capability flags */
    H5VL_bypass_init,                                   /* initialize   */
    H5VL_bypass_term,                                   /* terminate    */
    {                                           /* info_cls */
        sizeof(H5VL_bypass_info_t),                     /* size    */
        H5VL_bypass_info_copy,                          /* copy    */
        H5VL_bypass_info_cmp,                           /* compare */
        H5VL_bypass_info_free,                          /* free    */
        H5VL_bypass_info_to_str,                        /* to_str  */
        H5VL_bypass_str_to_info                         /* from_str */
    },
    {                                           /* wrap_cls */
        H5VL_bypass_get_object,                         /* get_object   */
        H5VL_bypass_get_wrap_ctx,                       /* get_wrap_ctx */
        H5VL_bypass_wrap_object,                        /* wrap_object  */
        H5VL_bypass_unwrap_object,                      /* unwrap_object */
        H5VL_bypass_free_wrap_ctx                       /* free_wrap_ctx */
    },
    {                                           /* attribute_cls */
        H5VL_bypass_attr_create,                        /* create */
        H5VL_bypass_attr_open,                          /* open */
        H5VL_bypass_attr_read,                          /* read */
        H5VL_bypass_attr_write,                         /* write */
        H5VL_bypass_attr_get,                           /* get */
        H5VL_bypass_attr_specific,                      /* specific */
        H5VL_bypass_attr_optional,                      /* optional */
        H5VL_bypass_attr_close                          /* close */
    },
    {                                           /* dataset_cls */
        H5VL_bypass_dataset_create,                     /* create */
        H5VL_bypass_dataset_open,                       /* open */
        H5VL_bypass_dataset_read,                       /* read */
        H5VL_bypass_dataset_write,                      /* write */
        H5VL_bypass_dataset_get,                        /* get */
        H5VL_bypass_dataset_specific,                   /* specific */
        H5VL_bypass_dataset_optional,                   /* optional */
        H5VL_bypass_dataset_close                       /* close */
    },
    {                                           /* datatype_cls */
        H5VL_bypass_datatype_commit,                    /* commit */
        H5VL_bypass_datatype_open,                      /* open */
        H5VL_bypass_datatype_get,                       /* get_size */
        H5VL_bypass_datatype_specific,                  /* specific */
        H5VL_bypass_datatype_optional,                  /* optional */
        H5VL_bypass_datatype_close                      /* close */
    },
    {                                           /* file_cls */
        H5VL_bypass_file_create,                        /* create */
        H5VL_bypass_file_open,                          /* open */
        H5VL_bypass_file_get,                           /* get */
        H5VL_bypass_file_specific,                      /* specific */
        H5VL_bypass_file_optional,                      /* optional */
        H5VL_bypass_file_close                          /* close */
    },
    {                                           /* group_cls */
        H5VL_bypass_group_create,                       /* create */
        H5VL_bypass_group_open,                         /* open */
        H5VL_bypass_group_get,                          /* get */
        H5VL_bypass_group_specific,                     /* specific */
        H5VL_bypass_group_optional,                     /* optional */
        H5VL_bypass_group_close                         /* close */
    },
    {                                           /* link_cls */
        H5VL_bypass_link_create,                        /* create */
        H5VL_bypass_link_copy,                          /* copy */
        H5VL_bypass_link_move,                          /* move */
        H5VL_bypass_link_get,                           /* get */
        H5VL_bypass_link_specific,                      /* specific */
        H5VL_bypass_link_optional                       /* optional */
    },
    {                                           /* object_cls */
        H5VL_bypass_object_open,                        /* open */
        H5VL_bypass_object_copy,                        /* copy */
        H5VL_bypass_object_get,                         /* get */
        H5VL_bypass_object_specific,                    /* specific */
        H5VL_bypass_object_optional                     /* optional */
    },
    {                                           /* introspect_cls */
        H5VL_bypass_introspect_get_conn_cls,            /* get_conn_cls */
        H5VL_bypass_introspect_get_cap_flags,           /* get_cap_flags */
        H5VL_bypass_introspect_opt_query,               /* opt_query */
    },
    {                                           /* request_cls */
        H5VL_bypass_request_wait,                       /* wait */
        H5VL_bypass_request_notify,                     /* notify */
        H5VL_bypass_request_cancel,                     /* cancel */
        H5VL_bypass_request_specific,                   /* specific */
        H5VL_bypass_request_optional,                   /* optional */
        H5VL_bypass_request_free                        /* free */
    },
    {                                           /* blob_cls */
        H5VL_bypass_blob_put,                           /* put */
        H5VL_bypass_blob_get,                           /* get */
        H5VL_bypass_blob_specific,                      /* specific */
        H5VL_bypass_blob_optional                       /* optional */
    },
    {                                           /* token_cls */
        H5VL_bypass_token_cmp,                          /* cmp */
        H5VL_bypass_token_to_str,                       /* to_str */
        H5VL_bypass_token_from_str                      /* from_str */
    },
    H5VL_bypass_optional                                /* optional */
};

/* The connector identification number, initialized at runtime */
static hid_t H5VL_BYPASS_g = H5I_INVALID_HID;

/* Required shim routines, to enable dynamic loading of shared library */
/* The HDF5 library _must_ find routines with these names and signatures
 *      for a shared library that contains a VOL connector to be detected
 *      and loaded at runtime.
 */
H5PL_type_t H5PLget_plugin_type(void) {return H5PL_TYPE_VOL;}
const void *H5PLget_plugin_info(void) {return &H5VL_bypass_g;}

static void* start_thread_for_pool(void* args);


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_new_obj
 *
 * Purpose:     Create a new bypass object for an underlying object
 *
 * Return:      Success:    Pointer to the new bypass object
 *              Failure:    NULL
 *
 * Programmer:  Quincey Koziol
 *              Monday, December 3, 2018
 *
 *-------------------------------------------------------------------------
 */
static H5VL_bypass_t *
H5VL_bypass_new_obj(void *under_obj, hid_t under_vol_id)
{
    H5VL_bypass_t *new_obj;

    new_obj = (H5VL_bypass_t *)calloc(1, sizeof(H5VL_bypass_t));
    new_obj->under_object = under_obj;
    new_obj->under_vol_id = under_vol_id;
    H5Iinc_ref(new_obj->under_vol_id);

    return new_obj;
} /* end H5VL_bypass_obj() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_free_obj
 *
 * Purpose:     Release a bypass object
 *
 * Note:	Take care to preserve the current HDF5 error stack
 *		when calling HDF5 API calls.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 * Programmer:  Quincey Koziol
 *              Monday, December 3, 2018
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_free_obj(H5VL_bypass_t *obj)
{
    hid_t err_id;

    err_id = H5Eget_current_stack();

    H5Idec_ref(obj->under_vol_id);

    H5Eset_current_stack(err_id);

    free(obj);

    return 0;
} /* end H5VL_bypass_free_obj() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_register
 *
 * Purpose:     Register the bypass VOL connector and retrieve an ID
 *              for it.
 *
 * Return:      Success:    The ID for the bypass VOL connector
 *              Failure:    -1
 *
 * Programmer:  Quincey Koziol
 *              Wednesday, November 28, 2018
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5VL_bypass_register(void)
{
    /* Singleton register the bypass VOL connector ID */
    if(H5VL_BYPASS_g < 0)
        H5VL_BYPASS_g = H5VLregister_connector(&H5VL_bypass_g, H5P_DEFAULT);

    return H5VL_BYPASS_g;
} /* end H5VL_bypass_register() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_init
 *
 * Purpose:     Initialize this VOL connector, performing any necessary
 *              operations for the connector that will apply to all containers
 *              accessed with the connector.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_init(hid_t vipl_id)
{
    char *nthreads_str = NULL;
    char *nsteps_str   = NULL;
    //info_for_thread_t info_for_thread[nthreads_tpool]; /* Remove it and use the global variable meta_for_thread? */
    int i;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL INIT\n");
#endif

    /* Shut compiler up about unused parameter */
    (void)vipl_id;

    /* Memory allocation for some information structures */
    file_stuff = (file_t *)calloc(file_stuff_size, sizeof(file_t));
    dset_stuff = (dset_t *)calloc(dset_info_size, sizeof(dset_t));
    info_stuff = (info_t *)calloc(info_size, sizeof(info_t));

    /* Retrieve the number of threads for the thread pool from the user's input */
    nthreads_str = getenv("BYPASS_VOL_NTHREADS");

    if (nthreads_str)
        nthreads_tpool = atoi(nthreads_str); 

    /* The minimal number of threads is 1 while the maximal is 32 */
    if (nthreads_tpool < 1)
        nthreads_tpool = 1;
    else if (nthreads_tpool > 32)
        nthreads_tpool = 32;

    /* Retrieve the number of steps for the thread pool from the user's input.
     * The thread pool accumulates the number of steps (jobs) in the queue before
     * signalling the threads to process them.
     */ 
    nsteps_str = getenv("BYPASS_VOL_NSTEPS");

    if (nsteps_str)
        nsteps_tpool = atoi(nsteps_str);

    /* The smallest step is 1 */   
    if (nsteps_tpool < 1)
        nsteps_tpool = 1;
  
//printf("%s at line %d: nthreads_tpool = %d, nsteps_tpool = %d\n", __func__, __LINE__, nthreads_tpool, nsteps_tpool);

    /* Initialize the information strcuture for threads to use.  Do it before starting threads */
    md_for_thread.file_indices = md_for_thread.file_indices_local;
    md_for_thread.addrs = md_for_thread.addrs_local;
    md_for_thread.sizes = md_for_thread.sizes_local;
    md_for_thread.vec_bufs = md_for_thread.vec_bufs_local;
    md_for_thread.vec_arr_nalloc = LOCAL_VECTOR_LEN;
    md_for_thread.vec_arr_nused  = 0;
    md_for_thread.free_memory = false;


    info_for_thread = malloc(nthreads_tpool * sizeof(info_for_thread_t));

    pthread_mutex_init(&mutex_local, NULL);
    pthread_cond_init(&cond_local, NULL);
    pthread_cond_init(&continue_local, NULL);

    /* Start threads for the thread pool to process the data */
    for (i = 0; i < nthreads_tpool; i++) {
	info_for_thread[i].thread_id = i; /* Remove info_for_thread and pass in the thread_id directly to pthread_create */

	if (pthread_create(&th[i], NULL, &start_thread_for_pool, &info_for_thread[i]) != 0)
	    fprintf(stderr, "failed to create thread %d\n", i);
    }

    return 0;
} /* end H5VL_bypass_init() */


/*---------------------------------------------------------------------------
 * Function:    H5VL_bypass_term
 *
 * Purpose:     Terminate this VOL connector, performing any necessary
 *              operations for the connector that release connector-wide
 *              resources (usually created / initialized with the 'init'
 *              callback).
 *
 * Return:      Success:    0
 *              Failure:    (Can't fail)
 *
 *---------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_term(void)
{
    FILE *log_fp;
    int i;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL TERM\n");
#endif

    /* Reset VOL ID */
    H5VL_BYPASS_g = H5I_INVALID_HID;

    stop_tpool = true;

    /* If H5Dread isn't even called in the application, the thread pool is waiting for this condition variable.
     * This broadcast tells the thread pool to stop waiting.  Turning on the 'thread_task_finished' variable stops
     * the while loop for pthread_cond_wait in thread pool. */ 
    thread_task_finished = true;

    pthread_cond_broadcast(&cond_local);

    for (i = 0; i < nthreads_tpool; i++) {
	if (pthread_join(th[i], NULL) != 0)
	    fprintf(stderr, "failed to join thread %d\n", i);
    }

    pthread_mutex_destroy(&mutex_local);
    pthread_cond_destroy(&cond_local);
    pthread_cond_destroy(&continue_local);

    /* Open the log file and output the following info:
     * - file name
     * - dataset name
     * - dataset (for contiguous) or chunk (for chunked dataset) location in file
     * - data offset in file
     * - number of elements to be read
     * - data offset in memory
     */
    log_fp = fopen("info.log", "w");

    for (i = 0; i < info_count; i++) {
        /* The END_OF_READ flag is used for multiple H5Dread calls.  It indicates the end of one call with
         * the special symbols of '###\n' in the log file. */
        if (!info_stuff[i].end_of_read)
            fprintf(log_fp, "%s %s %llu %llu %llu %llu\n", info_stuff[i].file_name, info_stuff[i].dset_name,
                info_stuff[i].dset_loc, info_stuff[i].data_offset_file, info_stuff[i].nelmts, info_stuff[i].data_offset_mem);
        else
            fprintf(log_fp, "###\n");

	//printf("%s: %d, i = %d, end_of_read = %d\n", __func__, __LINE__, i, info_stuff[i].end_of_read);
    }

    fclose(log_fp);

    //printf("%s: %d, dset_count = %d, dset_stuff[0].space_id = %lld\n", __func__, __LINE__, dset_count, dset_stuff[0].space_id);

    if (file_stuff)
        free(file_stuff);
    if (info_stuff)
        free(info_stuff);
    if (dset_stuff)
        free(dset_stuff);
    if (info_for_thread)
        free(info_for_thread);

    /* Wait until all threads finish before releasing resources */
    if (md_for_thread.free_memory) {
	if (md_for_thread.file_indices)
	    free(md_for_thread.file_indices);
	if (md_for_thread.addrs)
	    free(md_for_thread.addrs);
	if (md_for_thread.sizes)
	    free(md_for_thread.sizes);
	if (md_for_thread.vec_bufs)
	    free(md_for_thread.vec_bufs);
    }

    return 0;
} /* end H5VL_bypass_term() */


/*---------------------------------------------------------------------------
 * Function:    H5VL_bypass_info_copy
 *
 * Purpose:     Duplicate the connector's info object.
 *
 * Returns:     Success:    New connector info object
 *              Failure:    NULL
 *
 *---------------------------------------------------------------------------
 */
static void *
H5VL_bypass_info_copy(const void *_info)
{
    const H5VL_bypass_info_t *info = (const H5VL_bypass_info_t *)_info;
    H5VL_bypass_info_t *new_info;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL INFO Copy\n");
#endif

    /* Allocate new VOL info struct for the bypass connector */
    new_info = (H5VL_bypass_info_t *)calloc(1, sizeof(H5VL_bypass_info_t));

    /* Increment reference count on underlying VOL ID, and copy the VOL info */
    new_info->under_vol_id = info->under_vol_id;
    H5Iinc_ref(new_info->under_vol_id);
    if(info->under_vol_info)
        H5VLcopy_connector_info(new_info->under_vol_id, &(new_info->under_vol_info), info->under_vol_info);

    return new_info;
} /* end H5VL_bypass_info_copy() */


/*---------------------------------------------------------------------------
 * Function:    H5VL_bypass_info_cmp
 *
 * Purpose:     Compare two of the connector's info objects, setting *cmp_value,
 *              following the same rules as strcmp().
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *---------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_info_cmp(int *cmp_value, const void *_info1, const void *_info2)
{
    const H5VL_bypass_info_t *info1 = (const H5VL_bypass_info_t *)_info1;
    const H5VL_bypass_info_t *info2 = (const H5VL_bypass_info_t *)_info2;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL INFO Compare\n");
#endif

    /* Sanity checks */
    assert(info1);
    assert(info2);

    /* Initialize comparison value */
    *cmp_value = 0;

    /* Compare under VOL connector classes */
    H5VLcmp_connector_cls(cmp_value, info1->under_vol_id, info2->under_vol_id);
    if(*cmp_value != 0)
        return 0;

    /* Compare under VOL connector info objects */
    H5VLcmp_connector_info(cmp_value, info1->under_vol_id, info1->under_vol_info, info2->under_vol_info);
    if(*cmp_value != 0)
        return 0;

    return 0;
} /* end H5VL_bypass_info_cmp() */


/*---------------------------------------------------------------------------
 * Function:    H5VL_bypass_info_free
 *
 * Purpose:     Release an info object for the connector.
 *
 * Note:	Take care to preserve the current HDF5 error stack
 *		when calling HDF5 API calls.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *---------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_info_free(void *_info)
{
    H5VL_bypass_info_t *info = (H5VL_bypass_info_t *)_info;
    hid_t err_id;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL INFO Free\n");
#endif

    err_id = H5Eget_current_stack();

    /* Release underlying VOL ID and info */
    if(info->under_vol_info)
        H5VLfree_connector_info(info->under_vol_id, info->under_vol_info);
    H5Idec_ref(info->under_vol_id);

    H5Eset_current_stack(err_id);

    /* Free bypass info object itself */
    free(info);

    return 0;
} /* end H5VL_bypass_info_free() */


/*---------------------------------------------------------------------------
 * Function:    H5VL_bypass_info_to_str
 *
 * Purpose:     Serialize an info object for this connector into a string
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *---------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_info_to_str(const void *_info, char **str)
{
    const H5VL_bypass_info_t *info = (const H5VL_bypass_info_t *)_info;
    H5VL_class_value_t under_value = (H5VL_class_value_t)-1;
    char *under_vol_string = NULL;
    size_t under_vol_str_len = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL INFO To String\n");
#endif

    /* Get value and string for underlying VOL connector */
    H5VLget_value(info->under_vol_id, &under_value);
    H5VLconnector_info_to_str(info->under_vol_info, info->under_vol_id, &under_vol_string);

    /* Determine length of underlying VOL info string */
    if(under_vol_string)
        under_vol_str_len = strlen(under_vol_string);

    /* Allocate space for our info */
    *str = (char *)H5allocate_memory(32 + under_vol_str_len, (hbool_t)0);
    assert(*str);

    /* Encode our info
     * Normally we'd use snprintf() here for a little extra safety, but that
     * call had problems on Windows until recently. So, to be as platform-independent
     * as we can, we're using sprintf() instead.
     */
    sprintf(*str, "under_vol=%u;under_info={%s}", (unsigned)under_value, (under_vol_string ? under_vol_string : ""));

    /* Release under VOL info string, if there is one */
    if(under_vol_string)
        H5free_memory(under_vol_string);

    return 0;
} /* end H5VL_bypass_info_to_str() */


/*---------------------------------------------------------------------------
 * Function:    H5VL_bypass_str_to_info
 *
 * Purpose:     Deserialize a string into an info object for this connector.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *---------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_str_to_info(const char *str, void **_info)
{
    H5VL_bypass_info_t *info;
    unsigned under_vol_value;
    const char *under_vol_info_start, *under_vol_info_end;
    hid_t under_vol_id;
    void *under_vol_info = NULL;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL INFO String To Info\n");
#endif

    /* Retrieve the underlying VOL connector value and info */
    sscanf(str, "under_vol=%u;", &under_vol_value);
    under_vol_id = H5VLregister_connector_by_value((H5VL_class_value_t)under_vol_value, H5P_DEFAULT);
    under_vol_info_start = strchr(str, '{');
    under_vol_info_end = strrchr(str, '}');
    assert(under_vol_info_end > under_vol_info_start);
    if(under_vol_info_end != (under_vol_info_start + 1)) {
        char *under_vol_info_str;

        under_vol_info_str = (char *)malloc((size_t)(under_vol_info_end - under_vol_info_start));
        memcpy(under_vol_info_str, under_vol_info_start + 1, (size_t)((under_vol_info_end - under_vol_info_start) - 1));
        *(under_vol_info_str + (under_vol_info_end - under_vol_info_start)) = '\0';

        H5VLconnector_str_to_info(under_vol_info_str, under_vol_id, &under_vol_info);

        free(under_vol_info_str);
    } /* end else */

    /* Allocate new bypass VOL connector info and set its fields */
    info = (H5VL_bypass_info_t *)calloc(1, sizeof(H5VL_bypass_info_t));
    info->under_vol_id = under_vol_id;
    info->under_vol_info = under_vol_info;

    /* Set return value */
    *_info = info;

    return 0;
} /* end H5VL_bypass_str_to_info() */


/*---------------------------------------------------------------------------
 * Function:    H5VL_bypass_get_object
 *
 * Purpose:     Retrieve the 'data' for a VOL object.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *---------------------------------------------------------------------------
 */
static void *
H5VL_bypass_get_object(const void *obj)
{
    const H5VL_bypass_t *o = (const H5VL_bypass_t *)obj;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL Get object\n");
#endif

    return H5VLget_object(o->under_object, o->under_vol_id);
} /* end H5VL_bypass_get_object() */


/*---------------------------------------------------------------------------
 * Function:    H5VL_bypass_get_wrap_ctx
 *
 * Purpose:     Retrieve a "wrapper context" for an object
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *---------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_get_wrap_ctx(const void *obj, void **wrap_ctx)
{
    const H5VL_bypass_t *o = (const H5VL_bypass_t *)obj;
    H5VL_bypass_wrap_ctx_t *new_wrap_ctx;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL WRAP CTX Get\n");
#endif

    /* Allocate new VOL object wrapping context for the bypass connector */
    new_wrap_ctx = (H5VL_bypass_wrap_ctx_t *)calloc(1, sizeof(H5VL_bypass_wrap_ctx_t));

    /* Increment reference count on underlying VOL ID, and copy the VOL info */
    new_wrap_ctx->under_vol_id = o->under_vol_id;
    H5Iinc_ref(new_wrap_ctx->under_vol_id);
    H5VLget_wrap_ctx(o->under_object, o->under_vol_id, &new_wrap_ctx->under_wrap_ctx);

    /* Set wrap context to return */
    *wrap_ctx = new_wrap_ctx;

    return 0;
} /* end H5VL_bypass_get_wrap_ctx() */


/*---------------------------------------------------------------------------
 * Function:    H5VL_bypass_wrap_object
 *
 * Purpose:     Use a "wrapper context" to wrap a data object
 *
 * Return:      Success:    Pointer to wrapped object
 *              Failure:    NULL
 *
 *---------------------------------------------------------------------------
 */
static void *
H5VL_bypass_wrap_object(void *obj, H5I_type_t obj_type, void *_wrap_ctx)
{
    H5VL_bypass_wrap_ctx_t *wrap_ctx = (H5VL_bypass_wrap_ctx_t *)_wrap_ctx;
    H5VL_bypass_t *new_obj;
    void *under;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL WRAP Object\n");
#endif

    /* Wrap the object with the underlying VOL */
    under = H5VLwrap_object(obj, obj_type, wrap_ctx->under_vol_id, wrap_ctx->under_wrap_ctx);
    if(under)
        new_obj = H5VL_bypass_new_obj(under, wrap_ctx->under_vol_id);
    else
        new_obj = NULL;

    return new_obj;
} /* end H5VL_bypass_wrap_object() */


/*---------------------------------------------------------------------------
 * Function:    H5VL_bypass_unwrap_object
 *
 * Purpose:     Unwrap a wrapped object, discarding the wrapper, but returning
 *		underlying object.
 *
 * Return:      Success:    Pointer to unwrapped object
 *              Failure:    NULL
 *
 *---------------------------------------------------------------------------
 */
static void *
H5VL_bypass_unwrap_object(void *obj)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    void *under;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL UNWRAP Object\n");
#endif

    /* Unrap the object with the underlying VOL */
    under = H5VLunwrap_object(o->under_object, o->under_vol_id);

    if(under)
        H5VL_bypass_free_obj(o);

    return under;
} /* end H5VL_bypass_unwrap_object() */


/*---------------------------------------------------------------------------
 * Function:    H5VL_bypass_free_wrap_ctx
 *
 * Purpose:     Release a "wrapper context" for an object
 *
 * Note:	Take care to preserve the current HDF5 error stack
 *		when calling HDF5 API calls.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *---------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_free_wrap_ctx(void *_wrap_ctx)
{
    H5VL_bypass_wrap_ctx_t *wrap_ctx = (H5VL_bypass_wrap_ctx_t *)_wrap_ctx;
    hid_t err_id;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL WRAP CTX Free\n");
#endif

    err_id = H5Eget_current_stack();

    /* Release underlying VOL ID and wrap context */
    if(wrap_ctx->under_wrap_ctx)
        H5VLfree_wrap_ctx(wrap_ctx->under_wrap_ctx, wrap_ctx->under_vol_id);
    H5Idec_ref(wrap_ctx->under_vol_id);

    H5Eset_current_stack(err_id);

    /* Free bypass wrap context object itself */
    free(wrap_ctx);

    return 0;
} /* end H5VL_bypass_free_wrap_ctx() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_attr_create
 *
 * Purpose:     Creates an attribute on an object.
 *
 * Return:      Success:    Pointer to attribute object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
H5VL_bypass_attr_create(void *obj, const H5VL_loc_params_t *loc_params,
    const char *name, hid_t type_id, hid_t space_id, hid_t acpl_id,
    hid_t aapl_id, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *attr;
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    void *under;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL ATTRIBUTE Create\n");
#endif

    under = H5VLattr_create(o->under_object, loc_params, o->under_vol_id, name, type_id, space_id, acpl_id, aapl_id, dxpl_id, req);
    if(under) {
        attr = H5VL_bypass_new_obj(under, o->under_vol_id);

        /* Check for async request */
        if(req && *req)
            *req = H5VL_bypass_new_obj(*req, o->under_vol_id);
    } /* end if */
    else
        attr = NULL;

    return (void*)attr;
} /* end H5VL_bypass_attr_create() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_attr_open
 *
 * Purpose:     Opens an attribute on an object.
 *
 * Return:      Success:    Pointer to attribute object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
H5VL_bypass_attr_open(void *obj, const H5VL_loc_params_t *loc_params,
    const char *name, hid_t aapl_id, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *attr;
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    void *under;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL ATTRIBUTE Open\n");
#endif

    under = H5VLattr_open(o->under_object, loc_params, o->under_vol_id, name, aapl_id, dxpl_id, req);
    if(under) {
        attr = H5VL_bypass_new_obj(under, o->under_vol_id);

        /* Check for async request */
        if(req && *req)
            *req = H5VL_bypass_new_obj(*req, o->under_vol_id);
    } /* end if */
    else
        attr = NULL;

    return (void *)attr;
} /* end H5VL_bypass_attr_open() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_attr_read
 *
 * Purpose:     Reads data from attribute.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_attr_read(void *attr, hid_t mem_type_id, void *buf,
    hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)attr;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL ATTRIBUTE Read\n");
#endif

    ret_value = H5VLattr_read(o->under_object, o->under_vol_id, mem_type_id, buf, dxpl_id, req);

    /* Check for async request */
    if(req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    return ret_value;
} /* end H5VL_bypass_attr_read() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_attr_write
 *
 * Purpose:     Writes data to attribute.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_attr_write(void *attr, hid_t mem_type_id, const void *buf,
    hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)attr;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL ATTRIBUTE Write\n");
#endif

    ret_value = H5VLattr_write(o->under_object, o->under_vol_id, mem_type_id, buf, dxpl_id, req);

    /* Check for async request */
    if(req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    return ret_value;
} /* end H5VL_bypass_attr_write() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_attr_get
 *
 * Purpose:     Gets information about an attribute
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_attr_get(void *obj, H5VL_attr_get_args_t *args, hid_t dxpl_id,
    void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL ATTRIBUTE Get\n");
#endif

    ret_value = H5VLattr_get(o->under_object, o->under_vol_id, args, dxpl_id, req);

    /* Check for async request */
    if(req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    return ret_value;
} /* end H5VL_bypass_attr_get() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_attr_specific
 *
 * Purpose:     Specific operation on attribute
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_attr_specific(void *obj, const H5VL_loc_params_t *loc_params,
    H5VL_attr_specific_args_t *args, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL ATTRIBUTE Specific\n");
#endif

    ret_value = H5VLattr_specific(o->under_object, loc_params, o->under_vol_id, args, dxpl_id, req);

    /* Check for async request */
    if(req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    return ret_value;
} /* end H5VL_bypass_attr_specific() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_attr_optional
 *
 * Purpose:     Perform a connector-specific operation on an attribute
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_attr_optional(void *obj, H5VL_optional_args_t *args,
    hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL ATTRIBUTE Optional\n");
#endif

    ret_value = H5VLattr_optional(o->under_object, o->under_vol_id, args, dxpl_id, req);

    /* Check for async request */
    if(req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    return ret_value;
} /* end H5VL_bypass_attr_optional() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_attr_close
 *
 * Purpose:     Closes an attribute.
 *
 * Return:      Success:    0
 *              Failure:    -1, attr not closed.
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_attr_close(void *attr, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)attr;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL ATTRIBUTE Close\n");
#endif

    ret_value = H5VLattr_close(o->under_object, o->under_vol_id, dxpl_id, req);

    /* Check for async request */
    if(req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    /* Release our wrapper, if underlying attribute was closed */
    if(ret_value >= 0)
        H5VL_bypass_free_obj(o);

    return ret_value;
} /* end H5VL_bypass_attr_close() */

/* Retrieve the name of the file to which an object belong.  The OBJ_TYPE should be the 
 * type of OBJ.  Valid types include H5I_FILE, H5I_GROUP, H5I_DATATYPE, H5I_DATASET, or
 * H5I_ATTR.  This function basically copies what H5Fget_name does. 
 */
static ssize_t
get_filename_helper(H5VL_bypass_t *obj, char *file_name, H5I_type_t obj_type, void **req)
{
    H5VL_file_get_args_t args;
    size_t name_len = 0;  /* Length of file name */
    ssize_t ret_value = -1;

    args.op_type       = H5VL_FILE_GET_NAME;
    args.args.get_name.type = obj_type;
    args.args.get_name.buf_size = 1024;
    args.args.get_name.buf  = file_name;
    args.args.get_name.file_name_len = &name_len;

    if (H5VL_bypass_file_get(obj, &args, H5P_DEFAULT, req) < 0) {
        printf("In %s of %s at line %d: H5VL_bypass_file_get failed\n", __func__, __FILE__, __LINE__);
        ret_value = -1;
        goto done;
    }

    ret_value = (ssize_t)name_len;

done:
    return ret_value;
} /* get_filename_helper */

static H5L_type_t
get_linkinfo_helper(void *obj, const char *name, void **req)
{
    H5VL_loc_params_t    loc_params;          /* Location parameters for object access */
    H5VL_link_get_args_t vol_cb_args;         /* Arguments to VOL callback */
    H5L_info2_t          linfo;
    H5L_type_t           ret_value = H5L_TYPE_ERROR;

    /* Set up location struct */
    loc_params.type                         = H5VL_OBJECT_BY_NAME;
    loc_params.obj_type                     = H5I_FILE; //H5I_get_type(loc_id);
    loc_params.loc_data.loc_by_name.name    = name;
    loc_params.loc_data.loc_by_name.lapl_id = H5P_DEFAULT;

    /* Set up VOL callback arguments */
    vol_cb_args.op_type             = H5VL_LINK_GET_INFO; 
    vol_cb_args.args.get_info.linfo = &linfo;
     
    /* Get the link information */
    // H5VL_bypass_link_get(void *obj, const H5VL_loc_params_t *loc_params, H5VL_link_get_args_t *args, hid_t dxpl_id, void **req);
    if (H5VL_bypass_link_get(obj, &loc_params, &vol_cb_args, H5P_DATASET_XFER_DEFAULT, req) < 0) {
        printf("In %s of %s at line %d: H5VL_bypass_get_link failed\n", __func__, __FILE__, __LINE__);
        ret_value = -1;
        goto done;
    }

    ret_value = linfo.type;

done:
    return ret_value;
}

static void
dset_open_helper(void *obj, const char *name, H5VL_bypass_t *dset, hid_t dxpl_id, void **req)
{
    H5VL_dataset_get_args_t get_args;
    hid_t dcpl_id = H5I_INVALID_HID;
    haddr_t addr;
    H5VL_optional_args_t                opt_args;
    H5VL_native_dataset_optional_args_t dset_opt_args;
    int num_filters = 0;
    H5T_class_t dtype_class;
    //H5L_type_t    link_type;
    char file_name[1024];

    /* Enlarge the size of the structure for dataset info and Re-allocate the memory */
    if (dset_count == dset_info_size) {
	dset_info_size  *= 2;
	dset_stuff = (dset_t *)realloc(dset_stuff, dset_info_size * sizeof(dset_t));
    }

    /* Retrieve dataset's DCPL, copied from H5Dget_create_plist */
    get_args.op_type               = H5VL_DATASET_GET_DCPL;
    get_args.args.get_dcpl.dcpl_id = H5I_INVALID_HID;

    if (H5VL_bypass_dataset_get(dset, &get_args, dxpl_id, req) < 0)
	puts("unable to get dataset DCPL");

    dset_stuff[dset_count].dcpl_id = dcpl_id = get_args.args.get_dcpl.dcpl_id;

    /* Figure out the dataset's layout */
    dset_stuff[dset_count].layout = H5Pget_layout(dcpl_id);

    /* Retrieve the dataset's datatype */
    get_args.op_type               = H5VL_DATASET_GET_TYPE;
    get_args.args.get_type.type_id = H5I_INVALID_HID;

    if (H5VL_bypass_dataset_get(dset, &get_args, dxpl_id, req) < 0)
	puts("unable to get dataset's datatype");

    dset_stuff[dset_count].dtype_id = get_args.args.get_type.type_id;

//printf("\n%s: %d, count=%d, dset_info_size=%d, layout=%d, location=%llu, name=%s, dtype_id = %llu, H5T_STD_REF_DSETREG = %llu, H5T_NATIVE_INT = %llu, dcpl_id = %llu\n", __func__, __LINE__, dset_count, dset_info_size, dset_stuff[dset_count].layout, addr, name, dset_stuff[dset_count].dtype_id, H5T_STD_REF_DSETREG, H5T_NATIVE_INT, dcpl_id);

    /* Figure out the dataset's dataspace */
    get_args.op_type                 = H5VL_DATASET_GET_SPACE;
    get_args.args.get_space.space_id = H5I_INVALID_HID;

    /* Retrieve the dataset's dataspace ID */
    if (H5VL_bypass_dataset_get(dset, &get_args, dxpl_id, req) < 0)
	puts("unable to get dataset's dataspace");

    dset_stuff[dset_count].space_id = get_args.args.get_space.space_id;

    /* Figure out the dataset's location in the file */
    dset_opt_args.get_offset.offset = &addr;
    opt_args.op_type                = H5VL_NATIVE_DATASET_GET_OFFSET;
    opt_args.args                   = &dset_opt_args;

    if (H5VL_bypass_dataset_optional(dset, &opt_args, dxpl_id, req) < 0)
	puts("unable to get dataset's location in file");

    dset_stuff[dset_count].location = addr;

    /* The HDF5 library adds a '/' in front of the dataset name (full pathname).
     * Make sure the dataset name has it. */
    if (name) {
	if (name[0] != '/')
	    sprintf(dset_stuff[dset_count].dset_name, "/%s", name);
	else
	    strcpy(dset_stuff[dset_count].dset_name, name);
    }

    /* Get the file name of the dataset */
    get_filename_helper(dset, file_name, H5I_DATASET, req);

    strcpy(dset_stuff[dset_count].file_name, file_name);
//fprintf(stderr, "%s at %d: dset name = %s, file_name = %s\n", __func__, __LINE__, name, file_name);

    /* Get the link info */
    //link_type = get_linkinfo_helper(obj, name, req);

//fprintf(stderr, "%s at %d: link_type = %d\n", __func__, __LINE__, link_type);

//printf("\n%s: %d, count=%d, dset_info_size=%d, layout=%d, location=%llu, name=%s, dtype_id = %llu, H5T_STD_REF_DSETREG = %llu, dcpl_id = %llu\n", __func__, __LINE__, dset_count, dset_info_size, dset_stuff[dset_count].layout, addr, name, dset_stuff[dset_count].dtype_id, H5T_STD_REF_DSETREG, dcpl_id);
//printf("\n%s: %d,  count=%d, dset_info_size=%d, layout=%d, location=%llu, dset_name=%s\n", __func__, __LINE__, dset_count, dset_info_size, dset_stuff[dset_count].layout, addr, dset_stuff[dset_count].dset_name);

    /* Turn on the flag for using the native function to handle filters, virtual dataset, or the datatypes other than atomic types
     * which include time, opaque, compound, reference, variable-length, array types */ 
    num_filters = H5Pget_nfilters(dset_stuff[dset_count].dcpl_id);

    dtype_class = H5Tget_class(dset_stuff[dset_count].dtype_id);

    //if (num_filters > 0 || H5D_VIRTUAL == dset_stuff[dset_count].layout || H5L_TYPE_EXTERNAL == link_type ||
    if (num_filters > 0 || H5D_VIRTUAL == dset_stuff[dset_count].layout ||
        H5T_TIME == dtype_class || H5T_OPAQUE == dtype_class || H5T_COMPOUND == dtype_class ||
        H5T_REFERENCE == dtype_class || H5T_VLEN == dtype_class || H5T_ARRAY == dtype_class)
        dset_stuff[dset_count].use_native = true;

    /* Increment the reference count for this dataset */
    dset_stuff[dset_count].ref_count++;

    /* Increment the number of dataset */
    dset_count++;
}


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_dataset_create
 *
 * Purpose:     Creates a dataset in a container
 *
 * Return:      Success:    Pointer to a dataset object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
H5VL_bypass_dataset_create(void *obj, const H5VL_loc_params_t *loc_params,
    const char *name, hid_t lcpl_id, hid_t type_id, hid_t space_id,
    hid_t dcpl_id, hid_t dapl_id, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *dset;
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    void *under;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL DATASET Create\n");
#endif

    under = H5VLdataset_create(o->under_object, loc_params, o->under_vol_id, name, lcpl_id, type_id, space_id, dcpl_id,  dapl_id, dxpl_id, req);
    if(under) {
        dset = H5VL_bypass_new_obj(under, o->under_vol_id);

        /* Check for async request */
        if(req && *req)
            *req = H5VL_bypass_new_obj(*req, o->under_vol_id);
    } /* end if */
    else {
        dset = NULL;
        goto done;
    }

    /* Save the dataset information for quick access later */
    dset_open_helper(obj, name, dset, dxpl_id, req);

done:
    return (void *)dset;
} /* end H5VL_bypass_dataset_create() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_dataset_open
 *
 * Purpose:     Opens a dataset in a container
 *
 * Return:      Success:    Pointer to a dataset object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
H5VL_bypass_dataset_open(void *obj, const H5VL_loc_params_t *loc_params,
    const char *name, hid_t dapl_id, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *dset;
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    void          *under;
    unsigned      i;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL DATASET Open\n");
#endif

    under = H5VLdataset_open(o->under_object, loc_params, o->under_vol_id, name, dapl_id, dxpl_id, req);
    if(under) {
        dset = H5VL_bypass_new_obj(under, o->under_vol_id);

        /* Check for async request */
        if(req && *req)
            *req = H5VL_bypass_new_obj(*req, o->under_vol_id);
    } /* end if */
    else {
        dset = NULL;
        goto done;
    }

    /* If the dataset has already been opened, only increment the reference count of this dataset and finish */
    for (i = 0; i < dset_count; i++) {
        if (!strcmp(dset_stuff[i].dset_name, name)) {
            dset_stuff[i].ref_count++;
            
            goto done;
        }
    }

    /* Save the dataset information for quick access later */
    dset_open_helper(obj, name, dset, dxpl_id, req);

    for (i = 0; i < dset_count; i++)
        if (!strcmp(dset_stuff[i].dset_name, name))
            fprintf(stderr, "%s at %d: dset name = %s, file_name = %s\n", __func__, __LINE__, name, dset_stuff[i].file_name);

done:
    return (void *)dset;
} /* end H5VL_bypass_dataset_open() */

static ssize_t
get_dset_name_helper(H5VL_bypass_t *dset, char *name, void **req)
{
    H5VL_object_get_args_t args;
    H5VL_loc_params_t      loc_params;
    size_t                 dset_name_len = 0;  /* Length of file name */
    ssize_t ret_value = -1;

    /* Set location parameters */
    loc_params.type     = H5VL_OBJECT_BY_SELF;
    loc_params.obj_type = H5I_DATASET;

    /* Set up VOL callback arguments */
    args.op_type       = H5VL_OBJECT_GET_NAME;
    args.args.get_name.buf_size = 1024;
    args.args.get_name.buf  = name;
    args.args.get_name.name_len = &dset_name_len;

    if (H5VL_bypass_object_get(dset, &loc_params, &args, H5P_DATASET_XFER_DEFAULT, req) < 0) {
        printf("In %s of %s at line %d: H5VL_bypass_object_get failed\n", __func__, __FILE__, __LINE__);
        ret_value = -1;
        goto done;
    }

    ret_value = (ssize_t)dset_name_len;
done:
    return ret_value;
} /* get_dset_name_helper */

static herr_t
get_num_chunks_helper(H5VL_bypass_t *dset, hid_t file_space_id, hsize_t *nchunks, void **req)
{
    H5VL_optional_args_t                vol_cb_args;    /* Arguments to VOL callback */
    H5VL_native_dataset_optional_args_t dset_opt_args;  /* Arguments for optional operation */
    herr_t                              ret_value = 0;

    if (NULL == dset) {
        printf("In %s of %s at line %d: dset parameter can't be a null pointer\n", __func__, __FILE__, __LINE__);
        ret_value = -1;
        goto done;
    }

    if (NULL == nchunks) {
        printf("In %s of %s at line %d: nchunks parameter can't be a null pointer\n", __func__, __FILE__, __LINE__);
        ret_value = -1;
        goto done;
    }

    /* Set up VOL callback arguments */
    dset_opt_args.get_num_chunks.space_id = file_space_id;
    dset_opt_args.get_num_chunks.nchunks  = nchunks;
    vol_cb_args.op_type                   = H5VL_NATIVE_DATASET_GET_NUM_CHUNKS;
    vol_cb_args.args                      = &dset_opt_args;

    /* Get the number of written chunks */
    if (H5VL_bypass_dataset_optional(dset, &vol_cb_args, H5P_DEFAULT, req) < 0) {
        printf("In %s of %s at line %d: H5VL_bypass_dataset_optional failed\n", __func__, __FILE__, __LINE__);
        ret_value = -1;
        goto done;
    }

done:
    return ret_value;
} /* get_num_chunks_helper */

static herr_t
get_chunk_info_helper(H5VL_bypass_t *dset, hid_t fspace_id, hsize_t chk_index, hsize_t *offset /*out*/,
                  unsigned *filter_mask /*out*/, haddr_t *addr /*out*/, hsize_t *size /*out*/, void **req)
{
    H5VL_optional_args_t                vol_cb_args;    /* Arguments to VOL callback */
    H5VL_native_dataset_optional_args_t dset_opt_args;  /* Arguments for optional operation */
    hsize_t                             nchunks   = 0;  /* Number of chunks */
    herr_t                              ret_value = 0;

    /* Check arguments */
    if (NULL == dset) {
        printf("In %s of %s at line %d: dset parameter can't be a null pointer\n", __func__, __FILE__, __LINE__);
        ret_value = -1;
        goto done;
    }

    if (NULL == offset && NULL == filter_mask && NULL == addr && NULL == size) {
        printf("In %s of %s at line %d: invalid arguments, must have at least one non-null output argument\n", __func__, __FILE__, __LINE__);
        ret_value = -1;
        goto done;
    }

    /* Set up VOL callback arguments */
    dset_opt_args.get_num_chunks.space_id = fspace_id;
    dset_opt_args.get_num_chunks.nchunks  = &nchunks;
    vol_cb_args.op_type                   = H5VL_NATIVE_DATASET_GET_NUM_CHUNKS;
    vol_cb_args.args                      = &dset_opt_args;

    /* Get the number of written chunks to check range */
    if (H5VL_bypass_dataset_optional(dset, &vol_cb_args, H5P_DEFAULT, req) < 0) {
        printf("In %s of %s at line %d: H5VL_bypass_dataset_optional failed\n", __func__, __FILE__, __LINE__);
        ret_value = -1;
        goto done;
    }

    /* Check range for chunk index */
    if (chk_index >= nchunks) {
        printf("In %s of %s at line %d: chunk index is out of range\n", __func__, __FILE__, __LINE__);
        ret_value = -1;
        goto done;
    }

    /* Set up VOL callback arguments */
    dset_opt_args.get_chunk_info_by_idx.space_id    = fspace_id;
    dset_opt_args.get_chunk_info_by_idx.chk_index   = chk_index;
    dset_opt_args.get_chunk_info_by_idx.offset      = offset;
    dset_opt_args.get_chunk_info_by_idx.filter_mask = filter_mask;
    dset_opt_args.get_chunk_info_by_idx.addr        = addr;
    dset_opt_args.get_chunk_info_by_idx.size        = size;
    vol_cb_args.op_type                             = H5VL_NATIVE_DATASET_GET_CHUNK_INFO_BY_IDX;
    vol_cb_args.args                                = &dset_opt_args;

    /* Call private function to get the chunk info given the chunk's index */
    if (H5VL_bypass_dataset_optional(dset, &vol_cb_args, H5P_DEFAULT, req) < 0) {
        printf("In %s of %s at line %d: H5VL_bypass_dataset_optional failed\n", __func__, __FILE__, __LINE__);
        ret_value = -1;
        goto done;
    }

done:
    return ret_value;
} /* get_chunk_info_helper */

/* Figure out the data spaces in file and memory and make sure the selection is valid.
 * Below is the table borrowed from the reference manual entry for H5Dread:
 * 
 * mem_space_id	file_space_id	Behavior
 *
 * valid ID	valid ID	mem_space_id specifies the memory dataspace and the selection within it.
 *                              file_space_id specifies the selection within the file dataset's dataspace.
 * H5S_ALL	valid ID	The file dataset's dataspace is used for the memory dataspace and the selection
 *                              specified with file_space_id specifies the selection within it. The combination of
 *                              the file dataset's dataspace and the selection from file_space_id is used for memory also.
 * valid ID	H5S_ALL	        mem_space_id specifies the memory dataspace and the selection within it.
 *                              The selection within the file dataset's dataspace is set to the "all" selection.
 * H5S_ALL	H5S_ALL	        The file dataset's dataspace is used for the memory dataspace and the selection within
 *                              the memory dataspace is set to the "all" selection. The selection within the file dataset's
 *                              dataspace is set to the "all" selection.
 */
static herr_t
check_dspaces_helper(hid_t dset_space_id, hid_t file_space_id, hid_t *file_space_id_copy, hid_t mem_space_id, hid_t *mem_space_id_copy)
{
    herr_t ret_value = 0;

    /* Settle the file data space */
    if (H5S_ALL == file_space_id)
        /* Use the dataset's dataspace */
        *file_space_id_copy = dset_space_id;
    else
        /* Use the original data space passed in from H5Dread or H5Dread_multi */
        *file_space_id_copy = file_space_id;

    /* Settle the memory data space */
    if (H5S_ALL == mem_space_id)
        /* Use the file data space just settled above */
        *mem_space_id_copy = *file_space_id_copy;
    else
        /* Use the original data space passed in from H5Dread or H5Dread_multi */
        *mem_space_id_copy = mem_space_id;

    /* Make sure the selection + offset is within the extent of the dataspaces in file and memory */
    if (H5Sselect_valid(*file_space_id_copy) <= 0) {
        printf("In %s of %s at line %d: file data space selection isn't valid or H5Sselect_valide failed\n", __func__, __FILE__, __LINE__);
        ret_value = -1;
        goto done;
    }

    if (H5Sselect_valid(*mem_space_id_copy) <= 0) {
        printf("In %s of %s at line %d: memory data space selection isn't valid or H5Sselect_valide failed\n", __func__, __FILE__, __LINE__);
        ret_value = -1;
        goto done;
    }

done:
    return ret_value;
}

/* Break down into smaller sections if the data size is 2GB or bigger.  Need to change the type
 * of BUF to VOID to be more general */
static void
read_big_data(int fd, int *buf, size_t size, off_t offset)
{
    while (size > 0) {
        size_t bytes_in   = 0;  /* # of bytes to read       */
        size_t bytes_read = -1; /* # of bytes actually read */

        /* Trying to read more bytes than the return type can handle is
         * undefined behavior in POSIX.
         */
        if (size > POSIX_MAX_IO_BYTES)
            bytes_in = POSIX_MAX_IO_BYTES;
        else
            bytes_in = size;

        do {
            bytes_read = pread(fd, buf, bytes_in, offset);
            if (bytes_read > 0)
                offset += bytes_read;
        } while (-1 == bytes_read && EINTR == errno);

        size -= (size_t)bytes_read;
        buf = (int *)((char *)buf + bytes_read);
    } /* end while */
}

static
void* start_thread_for_pool(void* args)
{
    //int thread_id = ((info_for_thread_t *)args)->thread_id;
    //int fd = ((info_for_thread_t *)args)->fd;
    int      *file_indices_local;
    haddr_t  *addrs_local;
    size_t   *sizes_local;
    void     **vec_bufs_local;
    int      local_count = 0;
    int      i;

    //fprintf(stderr, "In start_thread_for_pool: %d\n", thread_id); 

    file_indices_local = (int *)malloc(nsteps_tpool * sizeof(int));
    addrs_local    = (haddr_t *)malloc(nsteps_tpool * sizeof(haddr_t));
    sizes_local    = (size_t *)malloc(nsteps_tpool * sizeof(size_t));
    vec_bufs_local = (void *)malloc(nsteps_tpool * sizeof(void *));

    /* Rename thread_loop_finish to a more appropriate name */
    while(!thread_loop_finish || !stop_tpool) {
        pthread_mutex_lock(&mutex_local);

//fprintf(stderr, "thread %d: before wait\n", thread_id); 
        while(thread_task_count == 0 && !thread_task_finished)
            pthread_cond_wait(&cond_local, &mutex_local);
//fprintf(stderr, "after wait\n"); 

        /* THREAD_TASK_COUNT can be smaller (LEFTOVER being passed into submit_task() in read_vectors()) or
         * larger (submit_task() keeps adding more before they are processed) than nsteps_tpool.
         * Choose the smaller value */
        local_count = MIN(thread_task_count, nsteps_tpool);

	for (i = 0; i < local_count; i++) {
	    file_indices_local[i] = md_for_thread.file_indices[info_pointer];
	    addrs_local[i]        = md_for_thread.addrs[info_pointer];
	    sizes_local[i]        = md_for_thread.sizes[info_pointer];
	    vec_bufs_local[i]     = md_for_thread.vec_bufs[info_pointer]; 

	    info_pointer++;
	    thread_task_count--;

	    file_stuff[file_indices_local[i]].num_reads++;
	    file_stuff[file_indices_local[i]].read_started = true;
	}

//fprintf(stderr, "thread %d: 1. local_count = %d, thread_task_count = %d, info_pointer = %d, addr = %llu, size = %lld, buf = %p, vec_arr_nused = %d\n", thread_id, local_count, thread_task_count, info_pointer - 1, addrs_local[0], sizes_local[0], vec_bufs_local[0], md_for_thread.vec_arr_nused);

        /* Turn on the flag thread_loop_finish if H5Dread finished putting all tasks in the queue (thread_task_finished is on) and 
         * the thread pool processed all the task in the queue */
	if (thread_task_finished && thread_task_count == 0)
	    thread_loop_finish = true;

        pthread_mutex_unlock(&mutex_local);

//fprintf(stderr, "before reading data\n");
        for (i = 0; i < local_count; i++) {
//fprintf(stderr, "thread_id = %d, i = %d: file_indices_local = %d, vec_bufs_local = %p, sizes_local = %ld, addrs_local = %llu\n", thread_id, i, file_indices_local[i], vec_bufs_local[i], sizes_local[i], addrs_local[i]);
            read_big_data(file_stuff[file_indices_local[i]].fd, vec_bufs_local[i], sizes_local[i], addrs_local[i]);

	    pthread_mutex_lock(&mutex_local);
	    file_stuff[file_indices_local[i]].num_reads--;

            /* When there is no task left in the queue and all the reads finish for the current file, signal the main process that
             * this file can be closed.
             */
	    if (thread_loop_finish && (file_stuff[file_indices_local[i]].num_reads == 0)) {
//fprintf(stderr, "thread %d: file name = %s, signal close_ready\n", thread_id, file_stuff[file_indices_local[i]].name);
	        pthread_cond_signal(&(file_stuff[file_indices_local[i]].close_ready));
	    }

	    pthread_mutex_unlock(&mutex_local);
        }

        /* If all task in the queue are finished and all the data read are finished, notify that
         * the corresponding file can be closed.
         */
	/* pthread_mutex_lock(&mutex_local);
        if (thread_loop_finish && (file_stuff[file_indices_local[0]].num_reads == 0)) {
fprintf(stderr, "thread %d: before broadcast\n", thread_id); 
	        pthread_cond_broadcast(&file_stuff[file_indices_local[0]].close_ready);
        }
	pthread_mutex_unlock(&mutex_local); */

        //pthread_mutex_unlock(&mutex_local);
//fprintf(stderr, "after reading data\n");
    }

    free(file_indices_local);
    free(addrs_local);
    free(sizes_local);
    free(vec_bufs_local);

    return NULL;
} /* end start_thread_for_pool() */

static void
process_vectors(void *rbuf, sel_info_t *selection_info)
{
    hid_t      file_iter_id, mem_iter_id;
    size_t     file_seq_i, mem_seq_i, file_nseq, mem_nseq;
    hssize_t   hss_nelmts;
    size_t     nelmts; 
    size_t     seq_nelem;
    hsize_t    file_off[SEL_SEQ_LIST_LEN], mem_off[SEL_SEQ_LIST_LEN];
    size_t     file_len[SEL_SEQ_LIST_LEN], mem_len[SEL_SEQ_LIST_LEN];
    size_t     io_len;
    int        local_count_for_signal = 0;

    /* Contiguous is treated as a single chunk */
    hss_nelmts = H5Sget_select_npoints(selection_info->file_space_id);
    nelmts = hss_nelmts;

    hss_nelmts = H5Sget_select_npoints(selection_info->mem_space_id);
    if (nelmts != hss_nelmts)
	printf("the number of selection in file (%ld) isn't equal to the number in memory (%lld)\n", nelmts, hss_nelmts); 

    file_iter_id = H5Ssel_iter_create(selection_info->file_space_id, selection_info->dtype_size, H5S_SEL_ITER_SHARE_WITH_DATASPACE);
    mem_iter_id  = H5Ssel_iter_create(selection_info->mem_space_id, selection_info->dtype_size, H5S_SEL_ITER_SHARE_WITH_DATASPACE);

    /* Initialize values so sequence lists are retrieved on the first iteration */
    file_seq_i = mem_seq_i = SEL_SEQ_LIST_LEN;
    file_nseq  = mem_nseq   = 0;

//printf("%s: %d\n", __func__, __LINE__);

    /* Loop until all elements are processed. The algorithm is copied from the function H5FD__read_selection_translate
     * in H5FDint.c, which is somewhat similar to H5D__select_io in H5Dselect.c */
    while (file_seq_i < file_nseq || nelmts > 0) {
	if (file_seq_i == SEL_SEQ_LIST_LEN) {
	    if (H5Ssel_iter_get_seq_list(file_iter_id, SEL_SEQ_LIST_LEN, SIZE_MAX, &file_nseq,
					      &seq_nelem, file_off, file_len) < 0)
		printf("file sequence length retrieval failed");

	    nelmts -= seq_nelem;
	    file_seq_i = 0;
	}

	/* Fill/refill memory sequence list if necessary */
	if (mem_seq_i == SEL_SEQ_LIST_LEN) {
	   if (H5Ssel_iter_get_seq_list(mem_iter_id, SEL_SEQ_LIST_LEN, SIZE_MAX, &mem_nseq, &seq_nelem,
					  mem_off, mem_len) < 0)
	       printf("memory sequence length retrieval failed");

	   mem_seq_i = 0;
	}

	/* Calculate length of this IO */
	io_len = MIN(file_len[file_seq_i], mem_len[mem_seq_i]);

        /* Make sure the data length isn't greater than 1MB, mainly for contiguous datasets.
         * To do: needs a flag to turn it on and off */
        io_len = MIN(io_len, MB);

	/* Lock md_for_thread and update it */
	pthread_mutex_lock(&mutex_local);

	if (md_for_thread.vec_arr_nused == md_for_thread.vec_arr_nalloc) {
	    /* Check if we're using the static arrays */
	    if (md_for_thread.addrs == md_for_thread.addrs_local) {
		/* Allocate dynamic arrays.  Need to free them later */
		if (NULL == (md_for_thread.file_indices = malloc(sizeof(md_for_thread.file_indices_local) * 2)))
		    printf("memory allocation failed for file ids list");
		if (NULL == (md_for_thread.addrs = malloc(sizeof(md_for_thread.addrs_local) * 2)))
		    printf("memory allocation failed for address list");
		if (NULL == (md_for_thread.sizes = malloc(sizeof(md_for_thread.sizes_local) * 2)))
		    printf("memory allocation failed for size list");
		if (NULL == (md_for_thread.vec_bufs = malloc(sizeof(md_for_thread.vec_bufs_local) * 2)))
		    printf("memory allocation failed for buffer list");

		/* Copy the existing data */
		(void)memcpy(md_for_thread.file_indices, md_for_thread.file_indices_local, sizeof(md_for_thread.file_indices_local));
		(void)memcpy(md_for_thread.addrs, md_for_thread.addrs_local, sizeof(md_for_thread.addrs_local));
		(void)memcpy(md_for_thread.sizes, md_for_thread.sizes_local, sizeof(md_for_thread.sizes_local));
		(void)memcpy(md_for_thread.vec_bufs, md_for_thread.vec_bufs_local, sizeof(md_for_thread.vec_bufs_local));
	    } else {
		void *tmp_ptr;

		/* Reallocate arrays */
		if (NULL == (tmp_ptr = realloc(md_for_thread.file_indices, md_for_thread.vec_arr_nalloc * sizeof(*(md_for_thread.file_indices)) * 2)))
		    printf("memory reallocation failed for file ids list");
		md_for_thread.file_indices = tmp_ptr;
		if (NULL == (tmp_ptr = realloc(md_for_thread.addrs, md_for_thread.vec_arr_nalloc * sizeof(*(md_for_thread.addrs)) * 2)))
		    printf("memory reallocation failed for address list");
		md_for_thread.addrs = tmp_ptr;
		if (NULL == (tmp_ptr = realloc(md_for_thread.sizes, md_for_thread.vec_arr_nalloc * sizeof(*(md_for_thread.sizes)) * 2)))
		    printf("memory reallocation failed for size list");
		md_for_thread.sizes = tmp_ptr;
		if (NULL == (tmp_ptr = realloc(md_for_thread.vec_bufs, md_for_thread.vec_arr_nalloc * sizeof(*(md_for_thread.vec_bufs)) * 2)))
		    printf("memory reallocation failed for buffer list");
		md_for_thread.vec_bufs = tmp_ptr;
	    }

	    /* Record that we've doubled the array sizes */
	    md_for_thread.vec_arr_nalloc *= 2;

	    md_for_thread.free_memory = true;
	}

	/* Add this segment to vector read list */
	md_for_thread.file_indices[md_for_thread.vec_arr_nused] = selection_info->my_file_index;
	md_for_thread.addrs[md_for_thread.vec_arr_nused]        = selection_info->chunk_addr + file_off[file_seq_i]; /* Add the base offset of the dataset to the address */
	md_for_thread.sizes[md_for_thread.vec_arr_nused]        = io_len;
	md_for_thread.vec_bufs[md_for_thread.vec_arr_nused]     = (void *)((uint8_t *)rbuf + mem_off[mem_seq_i]);

//fprintf(stderr, "In process_vector: %d. addr = %llu, sizes = %llu, buf = %p\n", md_for_thread.vec_arr_nused, md_for_thread.addrs[md_for_thread.vec_arr_nused], md_for_thread.sizes[md_for_thread.vec_arr_nused], md_for_thread.vec_bufs[md_for_thread.vec_arr_nused]);

	md_for_thread.vec_arr_nused++;

        /* Increment the task in the queue of the thread pool */
	thread_task_count++;

	local_count_for_signal++;

	pthread_mutex_unlock(&mutex_local);

	/* Let the queue accumulate nsteps_tpool entries then signal the thread pool to read them */
	if (local_count_for_signal >= nsteps_tpool) {
	    pthread_cond_broadcast(&cond_local);
	    local_count_for_signal = 0;        
	}

	/* Save the info for the C log file */
	{
	    /* Enlarge the size of the info for C and Re-allocate the memory if necessary */
	    if (info_count == info_size) {
		info_size  *= 2;
		info_stuff = (info_t *)realloc(info_stuff, info_size * sizeof(info_t));
	    }

	    /* Save the info in the structure */
	    strcpy(info_stuff[info_count].file_name, selection_info->file_name);
	    strcpy(info_stuff[info_count].dset_name, selection_info->dset_name);
	    info_stuff[info_count].dset_loc = selection_info->chunk_addr;
	    info_stuff[info_count].data_offset_file = file_off[file_seq_i] / selection_info->dtype_size;
	    info_stuff[info_count].real_offset = info_stuff[info_count].dset_loc + info_stuff[info_count].data_offset_file;
	    info_stuff[info_count].nelmts = io_len / selection_info->dtype_size;
	    info_stuff[info_count].data_offset_mem = mem_off[mem_seq_i] / selection_info->dtype_size;
            info_stuff[info_count].end_of_read = false;

	    //printf("%s: %d, info_count = %d, end_of_read = %d\n", __func__, __LINE__, info_count, info_stuff[info_count].end_of_read);

	    /* Increment the counter */
	    info_count++;
	}

	/* Update file sequence */
	if (io_len == file_len[file_seq_i])
	    file_seq_i++;
	else {                      
	    file_off[file_seq_i] += io_len;
	    file_len[file_seq_i] -= io_len;
	}                            
		
	/* Update memory sequence */
	if (io_len == mem_len[mem_seq_i])
	    mem_seq_i++;
	else {      
	    mem_off[mem_seq_i] += io_len;
	    mem_len[mem_seq_i] -= io_len;
	} 
    }

    /* If there is any leftover entries in the queue, signal the thread pool to read them */
    if (local_count_for_signal > 0 && local_count_for_signal < nsteps_tpool)
	pthread_cond_broadcast(&cond_local);

    H5Ssel_iter_close(file_iter_id);
    H5Ssel_iter_close(mem_iter_id);
} /* end process_vectors() */

static herr_t
process_chunks(void *rbuf, void *dset, hid_t dcpl_id, hid_t mem_space, hid_t file_space, sel_info_t *selection_info, void **req)
{
    hid_t      file_space_copy, mem_selection_id;
    hsize_t    chunk_dims[DIM_RANK_MAX];
    int        dset_dim_rank = 0;
    hsize_t    num_chunks = 0;
    haddr_t    chunk_addr;
    unsigned   filter_mask;
    hsize_t    chunk_offset[DIM_RANK_MAX], chunk_size;
    hssize_t   selection_offset[DIM_RANK_MAX];
    hssize_t   select_npoints = 0;
    H5S_sel_type select_type;
    hsize_t    dims_retrieved[DIM_RANK_MAX];
    hsize_t    offsets[DIM_RANK_MAX];
    int        i, j;
    herr_t     ret_value = 0;

    memset(offsets, 0, sizeof(hsize_t) * DIM_RANK_MAX);

    /* Maybe use the dataset's dataspace return from H5Dget_space */
    dset_dim_rank = H5Sget_simple_extent_ndims(file_space);

    H5Pget_chunk(dcpl_id, dset_dim_rank, chunk_dims);

    H5Sget_simple_extent_dims(file_space, dims_retrieved, NULL);

    get_num_chunks_helper(dset, file_space, &num_chunks, req);

    /* Iterate through all chunks and get the data selection falling into each chunk and the matching selection in memory */ 
    for (i = 0; i < num_chunks; i++) {
        get_chunk_info_helper(dset, file_space, i, chunk_offset, &filter_mask, &chunk_addr, &chunk_size, req);

        file_space_copy = H5Scopy(file_space);
        select_type     = H5Sget_select_type(file_space_copy);

        /* To calculate the intersection area in the next step, there must be a hyperslab selection to start with.
         * If there is no hyperslab selection in the file dataspace, select the whole dataspace. */
        if (H5S_SEL_HYPERSLABS != select_type && H5Sselect_hyperslab(file_space_copy, H5S_SELECT_SET, offsets, NULL, dims_retrieved, NULL) < 0) {
	    printf("In %s of %s at line %d: H5Sselect_hyperslab failed\n", __func__, __FILE__, __LINE__);
	    ret_value = -1;
	    goto done;
        }

        /* Get the intersection between file space selection and this chunk. In other words, 
         * get the file space selection falling into this chunk. */
        if (H5Sselect_hyperslab(file_space_copy, H5S_SELECT_AND, chunk_offset, NULL, chunk_dims, NULL) < 0) {
            printf("In %s of %s at line %d: H5Sselect_hyperslab failed\n", __func__, __FILE__, __LINE__);
            ret_value = -1;
            goto done;
        }

        select_npoints = H5Sget_select_npoints(file_space_copy);

        /* printf("\t\t2. chunk_offset={%llu, %llu}, chunk_dims={%llu, %llu}, chunk_addr = %llu, chunk_size = %llu, select_npoints = %llu\n", chunk_offset[0], chunk_offset[1], 
                       chunk_dims[0], chunk_dims[1], chunk_addr, chunk_size, select_npoints); */

        /* If the any selection falls into this chunk, save the selection information */
        if (select_npoints > 0) {
            /* Key function: get the data selection in memory which matches the file space selection falling into this chunk */ 
            mem_selection_id = H5Sselect_project_intersection(file_space, mem_space, file_space_copy);

            for(j = 0; j < dset_dim_rank; j++)
                selection_offset[j] = chunk_offset[j];    

            /* Move the file space selection in this chunk to upper-left corner and adjust (shrink) its extent to the size of the chunk.
             * In other words, the 'file_space_copy' that contains the data selection in file which falls into the current chunk is adjusted
             * from the size of the dataset to the size of chunk which still contains the same data selection. 'chunk_addr' is the original point
             * for 'file_space_copy'.
             */
            if (H5Sselect_adjust(file_space_copy, selection_offset) < 0) {
		printf("In %s of %s at line %d: H5Sselect_adjust failed\n", __func__, __FILE__, __LINE__);
		ret_value = -1;
		goto done;
	    }

            H5Sset_extent_simple(file_space_copy, dset_dim_rank, chunk_dims, chunk_dims);

            select_npoints = H5Sget_select_npoints(file_space_copy);
            //printf("\t\t5. chunk_offset={%llu, %llu}, chunk_addr = %llu, chunk_size = %llu, select_npoints = %llu\n", chunk_offset[0], chunk_offset[1], chunk_addr, chunk_size, select_npoints);
            //printf("\t\t select_npoints in memory = %llu\n", H5Sget_select_npoints(mem_selection_id));
            //printf("\t\t start[0] = %llu, start[1] = %llu, end[0] = %llu, end[1] = %llu\n", start[0], start[1], end[0], end[1]);

            /* Save the information for this chunk */
            selection_info->mem_space_id = mem_selection_id;
            selection_info->file_space_id = file_space_copy;
            selection_info->chunk_addr = chunk_addr;

            /* Retrieve the pieces of data (vectors) from the chunk and put them into the memory */
	    process_vectors(rbuf, selection_info);

            /* Close the space ID for the memory selection */
            H5Sclose(mem_selection_id);
        }

        /* Close the space ID for the file */
        H5Sclose(file_space_copy);
    }

done:
    return ret_value;
} /* end process_chunks() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_dataset_read
 *
 * Purpose:     Reads data elements from a dataset into a buffer.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_dataset_read(size_t count, void *dset[],
    hid_t mem_type_id[], hid_t mem_space_id[],
    hid_t file_space_id[], hid_t plist_id, void *buf[], void **req)
{
    void *o_arr[count];                     /* Array of under objects */
    hid_t under_vol_id;                     /* VOL ID for all objects */
    herr_t ret_value = 1;
    haddr_t dset_loc = 0;
    H5D_layout_t dset_layout = -1;
    hid_t dset_dtype_id;
    hid_t dset_space_id;
    hid_t file_space_id_copy, mem_space_id_copy;
    hid_t dcpl_id = H5I_INVALID_HID;
    hbool_t use_native = false;
    hbool_t dset_found = false;             /* True if a dataset is opened with H5Dopen. False if it's opened in other ways
                                             * like H5Rdeference (let native lib handle it).  For external link, it's also 
                                             * false because the path name is different. */
    char file_name[1024];
    char dset_name[1024];
    sel_info_t selection_info;
    int i, j;
    //hid_t  native_dtype;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL DATASET Read\n");
#endif

    /* Loop through all datasets and process them individually */
    for (j = 0; j < count; j++) {
	/* Retrieve the dataset's name */
	get_dset_name_helper((H5VL_bypass_t *)(dset[j]), dset_name, req);

	/* Find the dataset's info using its name */
	for (i = 0; i < dset_count; i++) {
	    if (!strcmp(dset_stuff[i].dset_name, dset_name)) {
                strcpy(file_name, dset_stuff[i].file_name);
		dcpl_id = dset_stuff[i].dcpl_id;
		dset_layout = dset_stuff[i].layout;
		dset_loc = dset_stuff[i].location;
		dset_dtype_id = dset_stuff[i].dtype_id;
		dset_space_id = dset_stuff[i].space_id;
		use_native = dset_stuff[i].use_native;
                dset_found = true;
	    }
	}

//printf("\n%s: %d, count=%lu, dset_name = %s, file_name = %s, dtype_id = %llu, H5T_STD_REF_DSETREG = %llu, H5T_NATIVE_INT = %llu, dcpl_id = %llu, H5I_INVALID_HID = %d\n", __func__, __LINE__, count, dset_name, file_name, dset_dtype_id, H5T_STD_REF_DSETREG, H5T_NATIVE_INT, dcpl_id, H5I_INVALID_HID);

	/* Let the native function handle datatype conversion.  Also check the flag for filters, virtual dataset and reference datatype */
	if (use_native || !dset_found || !H5Tequal(dset_dtype_id, mem_type_id[j])) {

	    /* Populate the array of under objects */
	    under_vol_id = ((H5VL_bypass_t *)(dset[0]))->under_vol_id;

	    o_arr[j] = ((H5VL_bypass_t *)(dset[j]))->under_object;
	    assert(under_vol_id == ((H5VL_bypass_t *)(dset[j]))->under_vol_id);
  
            //printf("%s: %d, in bypass VOL, count = %lu\n", __func__, __LINE__, count);

	    ret_value = H5VLdataset_read(1, &(o_arr[j]), under_vol_id, &(mem_type_id[j]), &(mem_space_id[j]), &(file_space_id[j]), plist_id, &(buf[j]), req);
	} else { /* Coming into Bypass VOL when no data conversion and filter */
	    //pthread_t th[nthreads_tpool];
	    //info_for_thread_t info_for_thread[nthreads_tpool]; /* Remove it and use the global variable meta_for_thread? */

            //printf("%s: %d, in bypass VOL\n", __func__, __LINE__);

            /* Decide the dataspaces in memory and file */
            if (check_dspaces_helper(dset_space_id, file_space_id[j], &file_space_id_copy, mem_space_id[j], &mem_space_id_copy) < 0) {
                printf("In %s of %s at line %d: can't figure out the data space in file or memory\n", __func__, __FILE__, __LINE__);
                ret_value = -1;
                goto done;
            }

	    /* Reset for the next H5Dread */
	    pthread_mutex_lock(&mutex_local);
	    thread_task_finished = false;
	    thread_loop_finish   = false;
	    pthread_mutex_unlock(&mutex_local);

	    /* Find out the file name */
	    //get_filename_helper((H5VL_bypass_t *)(dset[j]), file_name, H5I_DATASET, req);
//fprintf(stderr, "%s at %d: file_name = %s\n", __func__, __LINE__, file_name);

            selection_info.my_file_index = -1;

	    /* Find the correct data file */
	    for (i = 0; i < file_stuff_count; i++) {
		if (!strcmp(file_stuff[i].name, file_name)) {
		    selection_info.my_file_index = i;             /* Save this index in the list of FILE_T structures for quick lookup later */
                    break;
                }
            }

            if (selection_info.my_file_index == -1) {
	        printf("In %s of %s at line %d: can't find the file with the name %s\n", __func__, __FILE__, __LINE__, file_name);
	        ret_value = -1;
	        goto done;
            }

	    /* Initialize data selection info */
	    strcpy(selection_info.file_name, file_name);
	    strcpy(selection_info.dset_name, dset_name);

	    selection_info.dtype_size = H5Tget_size(dset_dtype_id);

	    if (H5D_CHUNKED == dset_layout) {
		/* Iterate through all chunks and map the data selection in each chunk to the memory.
		 * Put the selections into a queue for the thread pool to read the data */ 
		//process_chunks(buf[j], dset[j], dcpl_id, mem_space_id[j], file_space_id[j], &selection_info, req);
		process_chunks(buf[j], dset[j], dcpl_id, mem_space_id_copy, file_space_id_copy, &selection_info, req);
	    } else if (H5D_CONTIGUOUS == dset_layout) {
		selection_info.file_space_id = file_space_id_copy;
		selection_info.mem_space_id = mem_space_id_copy;
		selection_info.chunk_addr = dset_loc;

		/* Handles the hyperslab selection and read the data */
		process_vectors(buf[j], &selection_info);
	    }

	    /* Signal the thread pool to finish this read. Notify the thread pool that it finished putting tasks in the queue. */
	    pthread_mutex_lock(&mutex_local);
	    thread_task_finished = true;
	    pthread_mutex_unlock(&mutex_local);
	    pthread_cond_broadcast(&cond_local);  /* Why do this signal/broadcast? */

	    /* Save the info for the C log file */
	    {
		/* Enlarge the size of the info for C and Re-allocate the memory if necessary */
		if (info_count == info_size) {
		    info_size  *= 2;
		    info_stuff = (info_t *)realloc(info_stuff, info_size * sizeof(info_t));
		}

		/* Save the info in the structure */
		info_stuff[info_count].end_of_read = true;

		//printf("%s: %d, info_count = %d, end_of_read = %d\n", __func__, __LINE__, info_count, info_stuff[info_count].end_of_read);

		/* Increment the counter */
		info_count++;
	    }
	}

        dset_found = false;
    }

    /* Check for async request */
    if(req && *req)
        *req = H5VL_bypass_new_obj(*req, under_vol_id);

done:
    return ret_value;
} /* end H5VL_bypass_dataset_read() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_dataset_write
 *
 * Purpose:     Writes data elements from a buffer into a dataset.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_dataset_write(size_t count, void *dset[],
    hid_t mem_type_id[], hid_t mem_space_id[],
    hid_t file_space_id[], hid_t plist_id, const void *buf[], void **req)
{
    void *o_arr[count];   /* Array of under objects */
    hid_t under_vol_id;                     /* VOL ID for all objects */
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL DATASET Write\n");
#endif

    /* Populate the array of under objects */
    under_vol_id = ((H5VL_bypass_t *)(dset[0]))->under_vol_id;
    for(size_t u = 0; u < count; u++) {
        o_arr[u] = ((H5VL_bypass_t *)(dset[u]))->under_object;
        assert(under_vol_id == ((H5VL_bypass_t *)(dset[u]))->under_vol_id);
    }

    ret_value = H5VLdataset_write(count, o_arr, under_vol_id, mem_type_id, mem_space_id, file_space_id, plist_id, buf, req);

    /* Check for async request */
    if(req && *req)
        *req = H5VL_bypass_new_obj(*req, under_vol_id);

    return ret_value;
} /* end H5VL_bypass_dataset_write() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_dataset_get
 *
 * Purpose:     Gets information about a dataset
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_dataset_get(void *dset, H5VL_dataset_get_args_t *args,
    hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)dset;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL DATASET Get\n");
#endif

    ret_value = H5VLdataset_get(o->under_object, o->under_vol_id, args, dxpl_id, req);

    /* Check for async request */
    if(req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    return ret_value;
} /* end H5VL_bypass_dataset_get() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_dataset_specific
 *
 * Purpose:     Specific operation on a dataset
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_dataset_specific(void *obj, H5VL_dataset_specific_args_t *args,
    hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    hid_t under_vol_id;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL H5Dspecific\n");
#endif

    // Save copy of underlying VOL connector ID and prov helper, in case of
    // refresh destroying the current object
    under_vol_id = o->under_vol_id;

    ret_value = H5VLdataset_specific(o->under_object, o->under_vol_id, args, dxpl_id, req);

    /* Check for async request */
    if(req && *req)
        *req = H5VL_bypass_new_obj(*req, under_vol_id);

    return ret_value;
} /* end H5VL_bypass_dataset_specific() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_dataset_optional
 *
 * Purpose:     Perform a connector-specific operation on a dataset
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_dataset_optional(void *obj, H5VL_optional_args_t *args,
    hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL DATASET Optional\n");
#endif

    ret_value = H5VLdataset_optional(o->under_object, o->under_vol_id, args, dxpl_id, req);

    /* Check for async request */
    if(req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    return ret_value;
} /* end H5VL_bypass_dataset_optional() */

static void
remove_dset_info_helper(unsigned index)
{
    unsigned i;

    /* First, close the IDs related to the dataset */
    H5Pclose(dset_stuff[index].dcpl_id);
    dset_stuff[index].dcpl_id = H5I_INVALID_HID;

    H5Tclose(dset_stuff[index].dtype_id);
    dset_stuff[index].dtype_id = H5I_INVALID_HID;

    H5Sclose(dset_stuff[index].space_id);
    dset_stuff[index].space_id = H5I_INVALID_HID;

    dset_stuff[index].use_native = false;

    /* Remove the entry by shifting leftward all elements after this entry.
     * But don't do anything if this entry is the only one or is the last one in the array
     * except decrement the number of entries.
     */
    if (dset_count > 1 && index != dset_count -1 ) {
        for (i = index; i < dset_count - 1; i++) {
            strcpy(dset_stuff[i].dset_name, dset_stuff[i + 1].dset_name);
            strcpy(dset_stuff[i].file_name, dset_stuff[i + 1].file_name);
            dset_stuff[i].layout =          dset_stuff[i + 1].layout;
            dset_stuff[i].ref_count =       dset_stuff[i + 1].ref_count;
            dset_stuff[i].dcpl_id =         dset_stuff[i + 1].dcpl_id;
            dset_stuff[i].dtype_id =        dset_stuff[i + 1].dtype_id;
            dset_stuff[i].space_id =        dset_stuff[i + 1].space_id;
            dset_stuff[i].location =        dset_stuff[i + 1].location;
            dset_stuff[i].use_native =      dset_stuff[i + 1].use_native;
        }
    }

    dset_count--;
}


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_dataset_close
 *
 * Purpose:     Closes a dataset.
 *
 * Return:      Success:    0
 *              Failure:    -1, dataset not closed.
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_dataset_close(void *dset, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)dset;
    char dset_name[1024];
    unsigned i;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL DATASET Close\n");
#endif

    /* Retrieve the dataset's name */
    get_dset_name_helper(o, dset_name, req);

    /* Decrement the reference count.  Remove the dataset structure from the list when the reference count drops to zero */
    for (i = 0; i < dset_count; i++) {
        if (!strcmp(dset_stuff[i].dset_name, dset_name)) {
            dset_stuff[i].ref_count--;

            if (!dset_stuff[i].ref_count)
                remove_dset_info_helper(i);
        }
    }

    ret_value = H5VLdataset_close(o->under_object, o->under_vol_id, dxpl_id, req);

    /* Check for async request */
    if(req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    /* Release our wrapper, if underlying dataset was closed */
    if(ret_value >= 0)
        H5VL_bypass_free_obj(o);

    return ret_value;
} /* end H5VL_bypass_dataset_close() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_datatype_commit
 *
 * Purpose:     Commits a datatype inside a container.
 *
 * Return:      Success:    Pointer to datatype object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
H5VL_bypass_datatype_commit(void *obj, const H5VL_loc_params_t *loc_params,
    const char *name, hid_t type_id, hid_t lcpl_id, hid_t tcpl_id, hid_t tapl_id,
    hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *dt;
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    void *under;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL DATATYPE Commit\n");
#endif

    under = H5VLdatatype_commit(o->under_object, loc_params, o->under_vol_id, name, type_id, lcpl_id, tcpl_id, tapl_id, dxpl_id, req);
    if(under) {
        dt = H5VL_bypass_new_obj(under, o->under_vol_id);

        /* Check for async request */
        if(req && *req)
            *req = H5VL_bypass_new_obj(*req, o->under_vol_id);
    } /* end if */
    else
        dt = NULL;

    return (void *)dt;
} /* end H5VL_bypass_datatype_commit() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_datatype_open
 *
 * Purpose:     Opens a named datatype inside a container.
 *
 * Return:      Success:    Pointer to datatype object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
H5VL_bypass_datatype_open(void *obj, const H5VL_loc_params_t *loc_params,
    const char *name, hid_t tapl_id, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *dt;
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    void *under;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL DATATYPE Open\n");
#endif

    under = H5VLdatatype_open(o->under_object, loc_params, o->under_vol_id, name, tapl_id, dxpl_id, req);
    if(under) {
        dt = H5VL_bypass_new_obj(under, o->under_vol_id);

        /* Check for async request */
        if(req && *req)
            *req = H5VL_bypass_new_obj(*req, o->under_vol_id);
    } /* end if */
    else
        dt = NULL;

    return (void *)dt;
} /* end H5VL_bypass_datatype_open() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_datatype_get
 *
 * Purpose:     Get information about a datatype
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_datatype_get(void *dt, H5VL_datatype_get_args_t *args,
    hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)dt;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL DATATYPE Get\n");
#endif

    ret_value = H5VLdatatype_get(o->under_object, o->under_vol_id, args, dxpl_id, req);

    /* Check for async request */
    if(req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    return ret_value;
} /* end H5VL_bypass_datatype_get() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_datatype_specific
 *
 * Purpose:     Specific operations for datatypes
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_datatype_specific(void *obj, H5VL_datatype_specific_args_t *args,
    hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    hid_t under_vol_id;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL DATATYPE Specific\n");
#endif

    // Save copy of underlying VOL connector ID and prov helper, in case of
    // refresh destroying the current object
    under_vol_id = o->under_vol_id;

    ret_value = H5VLdatatype_specific(o->under_object, o->under_vol_id, args, dxpl_id, req);

    /* Check for async request */
    if(req && *req)
        *req = H5VL_bypass_new_obj(*req, under_vol_id);

    return ret_value;
} /* end H5VL_bypass_datatype_specific() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_datatype_optional
 *
 * Purpose:     Perform a connector-specific operation on a datatype
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_datatype_optional(void *obj, H5VL_optional_args_t *args,
    hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL DATATYPE Optional\n");
#endif

    ret_value = H5VLdatatype_optional(o->under_object, o->under_vol_id, args, dxpl_id, req);

    /* Check for async request */
    if(req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    return ret_value;
} /* end H5VL_bypass_datatype_optional() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_datatype_close
 *
 * Purpose:     Closes a datatype.
 *
 * Return:      Success:    0
 *              Failure:    -1, datatype not closed.
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_datatype_close(void *dt, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)dt;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL DATATYPE Close\n");
#endif

    assert(o->under_object);

    ret_value = H5VLdatatype_close(o->under_object, o->under_vol_id, dxpl_id, req);

    /* Check for async request */
    if(req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    /* Release our wrapper, if underlying datatype was closed */
    if(ret_value >= 0)
        H5VL_bypass_free_obj(o);

    return ret_value;
} /* end H5VL_bypass_datatype_close() */

static void
c_file_open_helper(const char *name)
{
    /* Enlarge the size of the file stuff for C and Re-allocate the memory if necessary */
    if (file_stuff_count == file_stuff_size) {
	file_stuff_size  *= 2;
	file_stuff = (file_t *)realloc(file_stuff, file_stuff_size * sizeof(file_t));
    }

    /* Open the file in C for IO without HDF5 */
    strcpy(file_stuff[file_stuff_count].name, name);

    //get_vfd_handle_helper(file, &(file_stuff[file_stuff_count].vfd_file_handle), req);

    /* VFD handle is not currently used */
    /* if (NULL == (file_stuff[file_stuff_count].vfd_file_handle = H5FDopen(name, H5F_ACC_RDONLY, H5P_DEFAULT, HADDR_UNDEF)))
	puts("failed to open VFD file");

    if (!file_stuff[file_stuff_count].vfd_file_handle)
	puts("failed to get VFD file handle"); */
   
    file_stuff[file_stuff_count].fd = open(name, O_RDONLY);
   
    /* Increment the reference count for this file */ 
    file_stuff[file_stuff_count].ref_count++;
   
    strcpy(file_stuff[file_stuff_count].name, name);

    file_stuff[file_stuff_count].num_reads = 0;
    file_stuff[file_stuff_count].read_started = false;
    pthread_cond_init(&(file_stuff[file_stuff_count].close_ready), NULL);  /* Initialize the condition variable for file closing */
    
    /*printf("%s: name = %s, file_stuff_count = %d, file_stuff[%d].name = %s, file_stuff[%d].fp = %d\n", __func__, name, file_stuff_count, file_stuff_count, file_stuff[file_stuff_count].name, file_stuff_count, file_stuff[file_stuff_count].fp);*/

    /* Increment the number of files being opened with C */
    file_stuff_count++;
}


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_file_create
 *
 * Purpose:     Creates a container using this connector
 *
 * Return:      Success:    Pointer to a file object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
H5VL_bypass_file_create(const char *name, unsigned flags, hid_t fcpl_id,
    hid_t fapl_id, hid_t dxpl_id, void **req)
{
    H5VL_bypass_info_t *info;
    H5VL_bypass_t *file;
    hid_t under_fapl_id;
    void *under;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL FILE Create\n");
#endif

    /* Get copy of our VOL info from FAPL */
    H5Pget_vol_info(fapl_id, (void **)&info);

    /* Make sure we have info about the underlying VOL to be used */
    if (!info)
        return NULL;

    /* Copy the FAPL */
    under_fapl_id = H5Pcopy(fapl_id);

    /* Set the VOL ID and info for the underlying FAPL */
    H5Pset_vol(under_fapl_id, info->under_vol_id, info->under_vol_info);

    /* Open the file with the underlying VOL connector */
    under = H5VLfile_create(name, flags, fcpl_id, under_fapl_id, dxpl_id, req);
    if(under) {
        file = H5VL_bypass_new_obj(under, info->under_vol_id);

        /* Check for async request */
        if(req && *req)
            *req = H5VL_bypass_new_obj(*req, info->under_vol_id);
    } /* end if */
    else
        file = NULL;

    /* Close underlying FAPL */
    H5Pclose(under_fapl_id);

    /* Release copy of our VOL info */
    H5VL_bypass_info_free(info);

    /* Open the C file and set the fields for the file_t structure */
    c_file_open_helper(name);

    return (void *)file;
} /* end H5VL_bypass_file_create() */

#ifdef TMP
static void
get_vfd_handle_helper(H5VL_bypass_t *obj, void **file_handle, void **req)
{
    H5VL_optional_args_t             vol_cb_args;         /* Arguments to VOL callback */
    H5VL_native_file_optional_args_t file_opt_args;       /* Arguments for optional operation */

    /* Set up VOL callback arguments */
    file_opt_args.get_vfd_handle.fapl_id     = H5P_DEFAULT;
    file_opt_args.get_vfd_handle.file_handle = file_handle;
    vol_cb_args.op_type                      = H5VL_NATIVE_FILE_GET_VFD_HANDLE;
    vol_cb_args.args                         = &file_opt_args;

    if (H5VL_bypass_file_optional(obj, &vol_cb_args, H5P_DEFAULT, req) < 0)
        puts("unable to get VFD file handle");
}
#endif


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_file_open
 *
 * Purpose:     Opens a container created with this connector
 *
 * Return:      Success:    Pointer to a file object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
H5VL_bypass_file_open(const char *name, unsigned flags, hid_t fapl_id,
    hid_t dxpl_id, void **req)
{
    H5VL_bypass_info_t *info;
    H5VL_bypass_t *file;
    hid_t under_fapl_id;
    void *under;
    unsigned i;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL FILE Open\n");
#endif

    /* Get copy of our VOL info from FAPL */
    H5Pget_vol_info(fapl_id, (void **)&info);

    /* Make sure we have info about the underlying VOL to be used */
    if (!info)
        return NULL;

    /* Copy the FAPL */
    under_fapl_id = H5Pcopy(fapl_id);

    /* Set the VOL ID and info for the underlying FAPL */
    H5Pset_vol(under_fapl_id, info->under_vol_id, info->under_vol_info);

    /* Open the file with the underlying VOL connector */
    under = H5VLfile_open(name, flags, under_fapl_id, dxpl_id, req);
    if(under) {
        file = H5VL_bypass_new_obj(under, info->under_vol_id);

        /* Check for async request */
        if(req && *req)
            *req = H5VL_bypass_new_obj(*req, info->under_vol_id);
    } /* end if */
    else
        file = NULL;

    /* Close underlying FAPL */
    H5Pclose(under_fapl_id);

    /* Release copy of our VOL info */
    H5VL_bypass_info_free(info);

    /* If the file has already been opened, only increment the reference count of this file and finish */
    for (i = 0; i < file_stuff_count; i++) {
        if (!strcmp(file_stuff[i].name, name) && file_stuff[i].fd) {
            file_stuff[i].ref_count++;
            
            goto done;
        }
    }

    /* Open the C file and set the fields for the file_t structure */
    c_file_open_helper(name);

done:
    return (void *)file;
} /* end H5VL_bypass_file_open() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_file_get
 *
 * Purpose:     Get info about a file
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_file_get(void *file, H5VL_file_get_args_t *args, hid_t dxpl_id,
    void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)file;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL FILE Get\n");
#endif

    ret_value = H5VLfile_get(o->under_object, o->under_vol_id, args, dxpl_id, req);

    /* Check for async request */
    if(req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    return ret_value;
} /* end H5VL_bypass_file_get() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_file_specific
 *
 * Purpose:     Specific operation on file
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_file_specific(void *file, H5VL_file_specific_args_t *args,
    hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)file;
    H5VL_bypass_t *new_o;
    H5VL_file_specific_args_t my_args;
    H5VL_file_specific_args_t *new_args;
    H5VL_bypass_info_t *info;
    hid_t under_vol_id = -1;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL FILE Specific\n");
#endif

    /* Check for 'is accessible' operation */
    if(args->op_type == H5VL_FILE_IS_ACCESSIBLE) {
        /* Make a (shallow) copy of the arguments */
        memcpy(&my_args, args, sizeof(my_args));

        /* Set up the new FAPL for the updated arguments */

        /* Get copy of our VOL info from FAPL */
        H5Pget_vol_info(args->args.is_accessible.fapl_id, (void **)&info);

        /* Make sure we have info about the underlying VOL to be used */
        if (!info)
            return (-1);

        /* Keep the correct underlying VOL ID for later */
        under_vol_id = info->under_vol_id;

        /* Copy the FAPL */
        my_args.args.is_accessible.fapl_id = H5Pcopy(args->args.is_accessible.fapl_id);

        /* Set the VOL ID and info for the underlying FAPL */
        H5Pset_vol(my_args.args.is_accessible.fapl_id, info->under_vol_id, info->under_vol_info);

        /* Set argument pointer to new arguments */
        new_args = &my_args;

        /* Set object pointer for operation */
        new_o = NULL;
    } /* end else-if */
    /* Check for 'delete' operation */
    else if(args->op_type == H5VL_FILE_DELETE) {
        /* Make a (shallow) copy of the arguments */
        memcpy(&my_args, args, sizeof(my_args));

        /* Set up the new FAPL for the updated arguments */

        /* Get copy of our VOL info from FAPL */
        H5Pget_vol_info(args->args.del.fapl_id, (void **)&info);

        /* Make sure we have info about the underlying VOL to be used */
        if (!info)
            return (-1);

        /* Keep the correct underlying VOL ID for later */
        under_vol_id = info->under_vol_id;

        /* Copy the FAPL */
        my_args.args.del.fapl_id = H5Pcopy(args->args.del.fapl_id);

        /* Set the VOL ID and info for the underlying FAPL */
        H5Pset_vol(my_args.args.del.fapl_id, info->under_vol_id, info->under_vol_info);

        /* Set argument pointer to new arguments */
        new_args = &my_args;

        /* Set object pointer for operation */
        new_o = NULL;
    } /* end else-if */
    else {
        /* Keep the correct underlying VOL ID for later */
        under_vol_id = o->under_vol_id;

        /* Set argument pointer to current arguments */
        new_args = args;

        /* Set object pointer for operation */
        new_o = o->under_object;
    } /* end else */

    ret_value = H5VLfile_specific(new_o, under_vol_id, new_args, dxpl_id, req);

    /* Check for async request */
    if(req && *req)
        *req = H5VL_bypass_new_obj(*req, under_vol_id);

    /* Check for 'is accessible' operation */
    if(args->op_type == H5VL_FILE_IS_ACCESSIBLE) {
        /* Close underlying FAPL */
        H5Pclose(my_args.args.is_accessible.fapl_id);

        /* Release copy of our VOL info */
        H5VL_bypass_info_free(info);
    } /* end else-if */
    /* Check for 'delete' operation */
    else if(args->op_type == H5VL_FILE_DELETE) {
        /* Close underlying FAPL */
        H5Pclose(my_args.args.del.fapl_id);

        /* Release copy of our VOL info */
        H5VL_bypass_info_free(info);
    } /* end else-if */
    else if(args->op_type == H5VL_FILE_REOPEN) {
        /* Wrap reopened file struct pointer, if we reopened one */
        if(ret_value >= 0 && args->args.reopen.file)
            *args->args.reopen.file = H5VL_bypass_new_obj(*args->args.reopen.file, o->under_vol_id);
    } /* end else */

    return ret_value;
} /* end H5VL_bypass_file_specific() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_file_optional
 *
 * Purpose:     Perform a connector-specific operation on a file
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_file_optional(void *file, H5VL_optional_args_t *args,
    hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)file;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL File Optional\n");
#endif

    ret_value = H5VLfile_optional(o->under_object, o->under_vol_id, args, dxpl_id, req);

    /* Check for async request */
    if(req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    return ret_value;
} /* end H5VL_bypass_file_optional() */

static void
remove_file_info_helper(unsigned index)
{
    unsigned i;

    /* Remove the entry by shifting leftward all elements after this entry.
     * But don't do anything if this entry is the only one or is the last one in the array
     * except decrement the number of entries.
     */
    if (file_stuff_count > 1 && index != file_stuff_count -1 ) {
        for (i = index; i < file_stuff_count - 1; i++) {
            strcpy(file_stuff[i].name, file_stuff[i + 1].name);
            file_stuff[i].fd =              file_stuff[i + 1].fd;
            /* file_stuff[i].vfd_file_handle = file_stuff[i + 1].vfd_file_handle; */
            file_stuff[i].ref_count =       file_stuff[i + 1].ref_count;
            file_stuff[i].num_reads =       file_stuff[i + 1].num_reads;
            file_stuff[i].read_started =    file_stuff[i + 1].read_started;
            file_stuff[i].close_ready =     file_stuff[i + 1].close_ready;
        }
    }

    file_stuff_count--;
}


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_file_close
 *
 * Purpose:     Closes a file.
 *
 * Return:      Success:    0
 *              Failure:    -1, file not closed.
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_file_close(void *file, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)file;
    char file_name[1024];
    herr_t ret_value;
    int i;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL FILE Close\n");
#endif

    /* Find the name of this file */
    get_filename_helper((H5VL_bypass_t *)file, file_name, H5I_FILE, req);
//fprintf(stderr, "%s at %d: file_name = %s\n", __func__, __LINE__, file_name);

    /* Close the file opened with C.  Remove the file structure from the list when the reference count drops to zero */
    for (i = 0; i < file_stuff_count; i++) {
        if (!strcmp(file_stuff[i].name, file_name) && file_stuff[i].fd) {
//fprintf(stderr, "%s at %d: file_name = %s, i = %d, file_stuff_count = %d, file_stuff[i].ref_count = %d, file_stuff[i].read_started = %d, num_reads = %d\n", __func__, __LINE__, file_name, i, file_stuff_count, file_stuff[i].ref_count, file_stuff[i].read_started, file_stuff[i].num_reads);
            /* Wait until all thread in the thread pool finish reading the data before closing the C file */
            if (file_stuff[i].read_started) {
		pthread_mutex_lock(&mutex_local);
		//while (!file_stuff[i].read_started || file_stuff[i].num_reads)
		while (file_stuff[i].num_reads)
		    pthread_cond_wait(&(file_stuff[i].close_ready), &mutex_local);
		pthread_mutex_unlock(&mutex_local);
            }

//fprintf(stderr, "%s at %d: file_name = %s, i = %d, file_stuff_count = %d, file_stuff[i].ref_count = %d, file_stuff[i].read_started = %d, num_reads = %d\n", __func__, __LINE__, file_name, i, file_stuff_count, file_stuff[i].ref_count, file_stuff[i].read_started, file_stuff[i].num_reads);

            file_stuff[i].ref_count--;

            /* When the reference count drops to zero, close the file */
            if (!file_stuff[i].ref_count) {
		close(file_stuff[i].fd);
		file_stuff[i].fd = -1;
		//H5FDclose(file_stuff[i].vfd_file_handle);
		pthread_cond_destroy(&(file_stuff[i].close_ready));

		/* Remove this file info structure from the list */
                remove_file_info_helper(i);
            }
        }
    }

    ret_value = H5VLfile_close(o->under_object, o->under_vol_id, dxpl_id, req);

    /* Check for async request */
    if(req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    /* Release our wrapper, if underlying file was closed */
    if(ret_value >= 0)
        H5VL_bypass_free_obj(o);

    return ret_value;
} /* end H5VL_bypass_file_close() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_group_create
 *
 * Purpose:     Creates a group inside a container
 *
 * Return:      Success:    Pointer to a group object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
H5VL_bypass_group_create(void *obj, const H5VL_loc_params_t *loc_params,
    const char *name, hid_t lcpl_id, hid_t gcpl_id, hid_t gapl_id,
    hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *group;
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    void *under;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL GROUP Create\n");
#endif

    under = H5VLgroup_create(o->under_object, loc_params, o->under_vol_id, name, lcpl_id, gcpl_id,  gapl_id, dxpl_id, req);
    if(under) {
        group = H5VL_bypass_new_obj(under, o->under_vol_id);

        /* Check for async request */
        if(req && *req)
            *req = H5VL_bypass_new_obj(*req, o->under_vol_id);
    } /* end if */
    else
        group = NULL;

    return (void *)group;
} /* end H5VL_bypass_group_create() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_group_open
 *
 * Purpose:     Opens a group inside a container
 *
 * Return:      Success:    Pointer to a group object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
H5VL_bypass_group_open(void *obj, const H5VL_loc_params_t *loc_params,
    const char *name, hid_t gapl_id, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *group;
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    void *under;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL GROUP Open\n");
#endif

    under = H5VLgroup_open(o->under_object, loc_params, o->under_vol_id, name, gapl_id, dxpl_id, req);
    if(under) {
        group = H5VL_bypass_new_obj(under, o->under_vol_id);

        /* Check for async request */
        if(req && *req)
            *req = H5VL_bypass_new_obj(*req, o->under_vol_id);
    } /* end if */
    else
        group = NULL;

    return (void *)group;
} /* end H5VL_bypass_group_open() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_group_get
 *
 * Purpose:     Get info about a group
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_group_get(void *obj, H5VL_group_get_args_t *args, hid_t dxpl_id,
    void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL GROUP Get\n");
#endif

    ret_value = H5VLgroup_get(o->under_object, o->under_vol_id, args, dxpl_id, req);

    /* Check for async request */
    if(req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    return ret_value;
} /* end H5VL_bypass_group_get() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_group_specific
 *
 * Purpose:     Specific operation on a group
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_group_specific(void *obj, H5VL_group_specific_args_t *args,
    hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    H5VL_group_specific_args_t my_args;
    H5VL_group_specific_args_t *new_args;
    hid_t under_vol_id;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL GROUP Specific\n");
#endif

    // Save copy of underlying VOL connector ID and prov helper, in case of
    // refresh destroying the current object
    under_vol_id = o->under_vol_id;

    /* Unpack arguments to get at the child file pointer when mounting a file */
    if(args->op_type == H5VL_GROUP_MOUNT) {

        /* Make a (shallow) copy of the arguments */
        memcpy(&my_args, args, sizeof(my_args));

        /* Set the object for the child file */
        my_args.args.mount.child_file = ((H5VL_bypass_t *)args->args.mount.child_file)->under_object;

        /* Point to modified arguments */
        new_args = &my_args;
    } /* end if */
    else
        new_args = args;

    ret_value = H5VLgroup_specific(o->under_object, under_vol_id, new_args, dxpl_id, req);

    /* Check for async request */
    if(req && *req)
        *req = H5VL_bypass_new_obj(*req, under_vol_id);

    return ret_value;
} /* end H5VL_bypass_group_specific() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_group_optional
 *
 * Purpose:     Perform a connector-specific operation on a group
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_group_optional(void *obj, H5VL_optional_args_t *args,
    hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL GROUP Optional\n");
#endif

    ret_value = H5VLgroup_optional(o->under_object, o->under_vol_id, args, dxpl_id, req);

    /* Check for async request */
    if(req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    return ret_value;
} /* end H5VL_bypass_group_optional() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_group_close
 *
 * Purpose:     Closes a group.
 *
 * Return:      Success:    0
 *              Failure:    -1, group not closed.
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_group_close(void *grp, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)grp;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL GROUP Close\n");
#endif

    ret_value = H5VLgroup_close(o->under_object, o->under_vol_id, dxpl_id, req);

    /* Check for async request */
    if(req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    /* Release our wrapper, if underlying file was closed */
    if(ret_value >= 0)
        H5VL_bypass_free_obj(o);

    return ret_value;
} /* end H5VL_bypass_group_close() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_link_create
 *
 * Purpose:     Creates a hard / soft / UD / external link.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_link_create(H5VL_link_create_args_t *args, void *obj,
    const H5VL_loc_params_t *loc_params, hid_t lcpl_id, hid_t lapl_id,
    hid_t dxpl_id, void **req)
{
    H5VL_link_create_args_t my_args;
    H5VL_link_create_args_t *new_args;
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    hid_t under_vol_id = -1;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL LINK Create\n");
#endif

    /* Try to retrieve the "under" VOL id */
    if(o)
        under_vol_id = o->under_vol_id;

    /* Fix up the link target object for hard link creation */
    if(H5VL_LINK_CREATE_HARD == args->op_type) {
        /* If it's a non-NULL pointer, find the 'under object' and re-set the args */
        if(args->args.hard.curr_obj) {
            /* Make a (shallow) copy of the arguments */
            memcpy(&my_args, args, sizeof(my_args));

            /* Check if we still need the "under" VOL ID */
            if(under_vol_id < 0)
                under_vol_id = ((H5VL_bypass_t *)args->args.hard.curr_obj)->under_vol_id;

            /* Set the object for the link target */
            my_args.args.hard.curr_obj = ((H5VL_bypass_t *)args->args.hard.curr_obj)->under_object;

            /* Set argument pointer to modified parameters */
            new_args = &my_args;
        } /* end if */
        else
            new_args = args;
    } /* end if */
    else
        new_args = args;

    /* Re-issue 'link create' call, possibly using the unwrapped pieces */
    ret_value = H5VLlink_create(new_args, (o ? o->under_object : NULL), loc_params, under_vol_id, lcpl_id, lapl_id, dxpl_id, req);

    /* Check for async request */
    if(req && *req)
        *req = H5VL_bypass_new_obj(*req, under_vol_id);

    return ret_value;
} /* end H5VL_bypass_link_create() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_link_copy
 *
 * Purpose:     Renames an object within an HDF5 container and copies it to a new
 *              group.  The original name SRC is unlinked from the group graph
 *              and then inserted with the new name DST (which can specify a
 *              new path for the object) as an atomic operation. The names
 *              are interpreted relative to SRC_LOC_ID and
 *              DST_LOC_ID, which are either file IDs or group ID.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_link_copy(void *src_obj, const H5VL_loc_params_t *loc_params1,
    void *dst_obj, const H5VL_loc_params_t *loc_params2, hid_t lcpl_id,
    hid_t lapl_id, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o_src = (H5VL_bypass_t *)src_obj;
    H5VL_bypass_t *o_dst = (H5VL_bypass_t *)dst_obj;
    hid_t under_vol_id = -1;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL LINK Copy\n");
#endif

    /* Retrieve the "under" VOL id */
    if(o_src)
        under_vol_id = o_src->under_vol_id;
    else if(o_dst)
        under_vol_id = o_dst->under_vol_id;
    assert(under_vol_id > 0);

    ret_value = H5VLlink_copy((o_src ? o_src->under_object : NULL), loc_params1, (o_dst ? o_dst->under_object : NULL), loc_params2, under_vol_id, lcpl_id, lapl_id, dxpl_id, req);

    /* Check for async request */
    if(req && *req)
        *req = H5VL_bypass_new_obj(*req, under_vol_id);

    return ret_value;
} /* end H5VL_bypass_link_copy() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_link_move
 *
 * Purpose:     Moves a link within an HDF5 file to a new group.  The original
 *              name SRC is unlinked from the group graph
 *              and then inserted with the new name DST (which can specify a
 *              new path for the object) as an atomic operation. The names
 *              are interpreted relative to SRC_LOC_ID and
 *              DST_LOC_ID, which are either file IDs or group ID.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_link_move(void *src_obj, const H5VL_loc_params_t *loc_params1,
    void *dst_obj, const H5VL_loc_params_t *loc_params2, hid_t lcpl_id,
    hid_t lapl_id, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o_src = (H5VL_bypass_t *)src_obj;
    H5VL_bypass_t *o_dst = (H5VL_bypass_t *)dst_obj;
    hid_t under_vol_id = -1;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL LINK Move\n");
#endif

    /* Retrieve the "under" VOL id */
    if(o_src)
        under_vol_id = o_src->under_vol_id;
    else if(o_dst)
        under_vol_id = o_dst->under_vol_id;
    assert(under_vol_id > 0);

    ret_value = H5VLlink_move((o_src ? o_src->under_object : NULL), loc_params1, (o_dst ? o_dst->under_object : NULL), loc_params2, under_vol_id, lcpl_id, lapl_id, dxpl_id, req);

    /* Check for async request */
    if(req && *req)
        *req = H5VL_bypass_new_obj(*req, under_vol_id);

    return ret_value;
} /* end H5VL_bypass_link_move() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_link_get
 *
 * Purpose:     Get info about a link
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_link_get(void *obj, const H5VL_loc_params_t *loc_params,
    H5VL_link_get_args_t *args, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL LINK Get\n");
#endif

    ret_value = H5VLlink_get(o->under_object, loc_params, o->under_vol_id, args, dxpl_id, req);

    /* Check for async request */
    if(req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    return ret_value;
} /* end H5VL_bypass_link_get() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_link_specific
 *
 * Purpose:     Specific operation on a link
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_link_specific(void *obj, const H5VL_loc_params_t *loc_params,
    H5VL_link_specific_args_t *args, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL LINK Specific\n");
#endif

    ret_value = H5VLlink_specific(o->under_object, loc_params, o->under_vol_id, args, dxpl_id, req);

    /* Check for async request */
    if(req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    return ret_value;
} /* end H5VL_bypass_link_specific() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_link_optional
 *
 * Purpose:     Perform a connector-specific operation on a link
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_link_optional(void *obj, const H5VL_loc_params_t *loc_params,
    H5VL_optional_args_t *args, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL LINK Optional\n");
#endif

    ret_value = H5VLlink_optional(o->under_object, loc_params, o->under_vol_id, args, dxpl_id, req);

    /* Check for async request */
    if(req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    return ret_value;
} /* end H5VL_bypass_link_optional() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_object_open
 *
 * Purpose:     Opens an object inside a container.
 *
 * Return:      Success:    Pointer to object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
H5VL_bypass_object_open(void *obj, const H5VL_loc_params_t *loc_params,
    H5I_type_t *opened_type, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *new_obj;
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    void *under;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL OBJECT Open\n");
#endif

    under = H5VLobject_open(o->under_object, loc_params, o->under_vol_id, opened_type, dxpl_id, req);
    if(under) {
        new_obj = H5VL_bypass_new_obj(under, o->under_vol_id);

        /* Check for async request */
        if(req && *req)
            *req = H5VL_bypass_new_obj(*req, o->under_vol_id);
    } /* end if */
    else
        new_obj = NULL;

    return (void *)new_obj;
} /* end H5VL_bypass_object_open() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_object_copy
 *
 * Purpose:     Copies an object inside a container.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_object_copy(void *src_obj, const H5VL_loc_params_t *src_loc_params,
    const char *src_name, void *dst_obj, const H5VL_loc_params_t *dst_loc_params,
    const char *dst_name, hid_t ocpypl_id, hid_t lcpl_id, hid_t dxpl_id,
    void **req)
{
    H5VL_bypass_t *o_src = (H5VL_bypass_t *)src_obj;
    H5VL_bypass_t *o_dst = (H5VL_bypass_t *)dst_obj;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL OBJECT Copy\n");
#endif

    ret_value = H5VLobject_copy(o_src->under_object, src_loc_params, src_name, o_dst->under_object, dst_loc_params, dst_name, o_src->under_vol_id, ocpypl_id, lcpl_id, dxpl_id, req);

    /* Check for async request */
    if(req && *req)
        *req = H5VL_bypass_new_obj(*req, o_src->under_vol_id);

    return ret_value;
} /* end H5VL_bypass_object_copy() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_object_get
 *
 * Purpose:     Get info about an object
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_object_get(void *obj, const H5VL_loc_params_t *loc_params, H5VL_object_get_args_t *args, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL OBJECT Get\n");
#endif

    ret_value = H5VLobject_get(o->under_object, loc_params, o->under_vol_id, args, dxpl_id, req);

    /* Check for async request */
    if(req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    return ret_value;
} /* end H5VL_bypass_object_get() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_object_specific
 *
 * Purpose:     Specific operation on an object
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_object_specific(void *obj, const H5VL_loc_params_t *loc_params,
    H5VL_object_specific_args_t *args, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    hid_t under_vol_id;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL OBJECT Specific\n");
#endif

    // Save copy of underlying VOL connector ID and prov helper, in case of
    // refresh destroying the current object
    under_vol_id = o->under_vol_id;

    ret_value = H5VLobject_specific(o->under_object, loc_params, o->under_vol_id, args, dxpl_id, req);

    /* Check for async request */
    if(req && *req)
        *req = H5VL_bypass_new_obj(*req, under_vol_id);

    return ret_value;
} /* end H5VL_bypass_object_specific() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_object_optional
 *
 * Purpose:     Perform a connector-specific operation for an object
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_object_optional(void *obj, const H5VL_loc_params_t *loc_params,
    H5VL_optional_args_t *args, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL OBJECT Optional\n");
#endif

    ret_value = H5VLobject_optional(o->under_object, loc_params, o->under_vol_id, args, dxpl_id, req);

    /* Check for async request */
    if(req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    return ret_value;
} /* end H5VL_bypass_object_optional() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_introspect_get_conn_clss
 *
 * Purpose:     Query the connector class.
 *
 * Return:      SUCCEED / FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VL_bypass_introspect_get_conn_cls(void *obj, H5VL_get_conn_lvl_t lvl,
    const H5VL_class_t **conn_cls)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL INTROSPECT GetConnCls\n");
#endif

    /* Check for querying this connector's class */
    if(H5VL_GET_CONN_LVL_CURR == lvl) {
        *conn_cls = &H5VL_bypass_g;
        ret_value = 0;
    } /* end if */
    else
        ret_value = H5VLintrospect_get_conn_cls(o->under_object, o->under_vol_id,
            lvl, conn_cls);

    return ret_value;
} /* end H5VL_bypass_introspect_get_conn_cls() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_introspect_get_cap_flags
 *
 * Purpose:     Query the capability flags for this connector and any
 *              underlying connector(s).
 *
 * Return:      SUCCEED / FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VL_bypass_introspect_get_cap_flags(const void *_info, uint64_t *cap_flags)
{
    const H5VL_bypass_info_t *info = (const H5VL_bypass_info_t *)_info;
    herr_t                          ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL INTROSPECT GetCapFlags\n");
#endif

    /* Invoke the query on the underlying VOL connector */
    ret_value = H5VLintrospect_get_cap_flags(info->under_vol_info, info->under_vol_id, cap_flags);

    /* Bitwise OR our capability flags in */
    if (ret_value >= 0)
        *cap_flags |= H5VL_bypass_g.cap_flags;

    return ret_value;
} /* end H5VL_bypass_introspect_ext_get_cap_flags() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_introspect_opt_query
 *
 * Purpose:     Query if an optional operation is supported by this connector
 *
 * Return:      SUCCEED / FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VL_bypass_introspect_opt_query(void *obj, H5VL_subclass_t cls,
    int op_type, uint64_t *flags)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL INTROSPECT OptQuery\n");
#endif

    ret_value = H5VLintrospect_opt_query(o->under_object, o->under_vol_id, cls,
        op_type, flags);

    return ret_value;
} /* end H5VL_bypass_introspect_opt_query() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_request_wait
 *
 * Purpose:     Wait (with a timeout) for an async operation to complete
 *
 * Note:        Releases the request if the operation has completed and the
 *              connector callback succeeds
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_request_wait(void *obj, uint64_t timeout,
    H5VL_request_status_t *status)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL REQUEST Wait\n");
#endif

    ret_value = H5VLrequest_wait(o->under_object, o->under_vol_id, timeout, status);

    return ret_value;
} /* end H5VL_bypass_request_wait() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_request_notify
 *
 * Purpose:     Registers a user callback to be invoked when an asynchronous
 *              operation completes
 *
 * Note:        Releases the request, if connector callback succeeds
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_request_notify(void *obj, H5VL_request_notify_t cb, void *ctx)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL REQUEST Notify\n");
#endif

    ret_value = H5VLrequest_notify(o->under_object, o->under_vol_id, cb, ctx);

    return ret_value;
} /* end H5VL_bypass_request_notify() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_request_cancel
 *
 * Purpose:     Cancels an asynchronous operation
 *
 * Note:        Releases the request, if connector callback succeeds
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_request_cancel(void *obj, H5VL_request_status_t *status)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL REQUEST Cancel\n");
#endif

    ret_value = H5VLrequest_cancel(o->under_object, o->under_vol_id, status);

    return ret_value;
} /* end H5VL_bypass_request_cancel() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_request_specific
 *
 * Purpose:     Specific operation on a request
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_request_specific(void *obj, H5VL_request_specific_args_t *args)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t ret_value = -1;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL REQUEST Specific\n");
#endif

    ret_value = H5VLrequest_specific(o->under_object, o->under_vol_id, args);

    return ret_value;
} /* end H5VL_bypass_request_specific() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_request_optional
 *
 * Purpose:     Perform a connector-specific operation for a request
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_request_optional(void *obj, H5VL_optional_args_t *args)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL REQUEST Optional\n");
#endif

    ret_value = H5VLrequest_optional(o->under_object, o->under_vol_id, args);

    return ret_value;
} /* end H5VL_bypass_request_optional() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_request_free
 *
 * Purpose:     Releases a request, allowing the operation to complete without
 *              application tracking
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_request_free(void *obj)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL REQUEST Free\n");
#endif

    ret_value = H5VLrequest_free(o->under_object, o->under_vol_id);

    if(ret_value >= 0)
        H5VL_bypass_free_obj(o);

    return ret_value;
} /* end H5VL_bypass_request_free() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_blob_put
 *
 * Purpose:     Handles the blob 'put' callback
 *
 * Return:      SUCCEED / FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VL_bypass_blob_put(void *obj, const void *buf, size_t size,
    void *blob_id, void *ctx)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL BLOB Put\n");
#endif

    ret_value = H5VLblob_put(o->under_object, o->under_vol_id, buf, size,
        blob_id, ctx);

    return ret_value;
} /* end H5VL_bypass_blob_put() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_blob_get
 *
 * Purpose:     Handles the blob 'get' callback
 *
 * Return:      SUCCEED / FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VL_bypass_blob_get(void *obj, const void *blob_id, void *buf,
    size_t size, void *ctx)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL BLOB Get\n");
#endif

    ret_value = H5VLblob_get(o->under_object, o->under_vol_id, blob_id, buf,
        size, ctx);

    return ret_value;
} /* end H5VL_bypass_blob_get() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_blob_specific
 *
 * Purpose:     Handles the blob 'specific' callback
 *
 * Return:      SUCCEED / FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VL_bypass_blob_specific(void *obj, void *blob_id,
    H5VL_blob_specific_args_t *args)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL BLOB Specific\n");
#endif

    ret_value = H5VLblob_specific(o->under_object, o->under_vol_id, blob_id, args);

    return ret_value;
} /* end H5VL_bypass_blob_specific() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_blob_optional
 *
 * Purpose:     Handles the blob 'optional' callback
 *
 * Return:      SUCCEED / FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VL_bypass_blob_optional(void *obj, void *blob_id, H5VL_optional_args_t *args)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL BLOB Optional\n");
#endif

    ret_value = H5VLblob_optional(o->under_object, o->under_vol_id, blob_id, args);

    return ret_value;
} /* end H5VL_bypass_blob_optional() */


/*---------------------------------------------------------------------------
 * Function:    H5VL_bypass_token_cmp
 *
 * Purpose:     Compare two of the connector's object tokens, setting
 *              *cmp_value, following the same rules as strcmp().
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *---------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_token_cmp(void *obj, const H5O_token_t *token1,
    const H5O_token_t *token2, int *cmp_value)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL TOKEN Compare\n");
#endif

    /* Sanity checks */
    assert(obj);
    assert(token1);
    assert(token2);
    assert(cmp_value);

    ret_value = H5VLtoken_cmp(o->under_object, o->under_vol_id, token1, token2, cmp_value);

    return ret_value;
} /* end H5VL_bypass_token_cmp() */


/*---------------------------------------------------------------------------
 * Function:    H5VL_bypass_token_to_str
 *
 * Purpose:     Serialize the connector's object token into a string.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *---------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_token_to_str(void *obj, H5I_type_t obj_type,
    const H5O_token_t *token, char **token_str)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL TOKEN To string\n");
#endif

    /* Sanity checks */
    assert(obj);
    assert(token);
    assert(token_str);

    ret_value = H5VLtoken_to_str(o->under_object, obj_type, o->under_vol_id, token, token_str);

    return ret_value;
} /* end H5VL_bypass_token_to_str() */


/*---------------------------------------------------------------------------
 * Function:    H5VL_bypass_token_from_str
 *
 * Purpose:     Deserialize the connector's object token from a string.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *---------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_token_from_str(void *obj, H5I_type_t obj_type,
    const char *token_str, H5O_token_t *token)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL TOKEN From string\n");
#endif

    /* Sanity checks */
    assert(obj);
    assert(token);
    assert(token_str);

    ret_value = H5VLtoken_from_str(o->under_object, obj_type, o->under_vol_id, token_str, token);

    return ret_value;
} /* end H5VL_bypass_token_from_str() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_optional
 *
 * Purpose:     Handles the generic 'optional' callback
 *
 * Return:      SUCCEED / FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VL_bypass_optional(void *obj, H5VL_optional_args_t *args, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS VOL generic Optional\n");
#endif

    ret_value = H5VLoptional(o->under_object, o->under_vol_id, args, dxpl_id, req);

    return ret_value;
} /* end H5VL_bypass_optional() */
