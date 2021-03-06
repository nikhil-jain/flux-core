#ifndef _FLUX_CORE_KVS_H
#define _FLUX_CORE_KVS_H

#include <flux/core.h>

#include "kvs_dir.h"
#include "kvs_lookup.h"
#include "kvs_classic.h"
#include "kvs_watch.h"
#include "kvs_txn.h"
#include "kvs_commit.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KVS_PRIMARY_NAMESPACE "primary"

enum {
    FLUX_KVS_READDIR = 1,
    FLUX_KVS_READLINK = 2,
    FLUX_KVS_TREEOBJ = 16,
    FLUX_KVS_APPEND = 32,
};

/* Namespace
 * - namespace create only creates the namespace on rank 0.  Other
 *   ranks initialize against that namespace the first time they use
 *   it.
 * - namespace remove marks the namespace for removal on all ranks.
 *   Garbage collection will happen in the background and the
 *   namespace will official be removed.  The removal is "eventually
 *   consistent".
 */
flux_future_t *flux_kvs_namespace_create (flux_t *h, const char *namespace,
                                          int flags);
flux_future_t *flux_kvs_namespace_remove (flux_t *h, const char *namespace);

/* Synchronization:
 * Process A commits data, then gets the store version V and sends it to B.
 * Process B waits for the store version to be >= V, then reads data.
 * To use an alternate namespace, set environment variable FLUX_KVS_NAMESPACE.
 */
int flux_kvs_get_version (flux_t *h, int *versionp);
int flux_kvs_wait_version (flux_t *h, int version);

/* Garbage collect the cache.  On the root node, drop all data that
 * doesn't have a reference in the namespace.  On other nodes, the entire
 * cache is dropped and will be reloaded on demand.
 * Returns -1 on error (errno set), 0 on success.
 */
int flux_kvs_dropcache (flux_t *h);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_KVS_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
