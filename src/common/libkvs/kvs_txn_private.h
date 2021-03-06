#ifndef _KVS_TXN_PRIVATE_H
#define _KVS_TXN_PRIVATE_H

int txn_get_op_count (flux_kvs_txn_t *txn);

json_t *txn_get_ops (flux_kvs_txn_t *txn);

int txn_get_op (flux_kvs_txn_t *txn, int index, json_t **op);

int txn_decode_op (json_t *op, const char **key, int *flags, json_t **dirent);

int txn_encode_op (const char *key, int flags, json_t *dirent, json_t **op);

#endif /* !_KVS_TXN_PRIVATE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
