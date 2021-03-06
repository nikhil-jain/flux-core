#ifndef _UTIL_BLOBREF_H
#define _UTIL_BLOBREF_H

#define BLOBREF_MAX_STRING_SIZE     72
#define BLOBREF_MAX_DIGEST_SIZE     32

#include <stdint.h>

typedef char blobref_t[BLOBREF_MAX_STRING_SIZE];

/* Convert a blobref string to hash digest.
 * The hash algorithm is selected by the blobref prefix.
 * Returns hash length on success, or -1 on error, with errno set.
 */
int blobref_strtohash (const char *blobref, void *hash, int size);

/* Convert a hash digest to null-terminated blobref string in 'blobref'.
 * The hash algorithm is selected by 'hashtype', e.g. "sha1".
 * Returns 0 on success, -1 on error, with errno set.
 */
int blobref_hashtostr (const char *hashtype,
                       const void *hash, int len,
                       blobref_t blobref);

/* Compute hash over data and return null-terminated blobref string in
 * 'blobref'.  The hash algorithm is selected by 'hashtype', e.g. "sha1".
 * Returns 0 on success, -1 on error with errno set.
 */
int blobref_hash (const char *hashtype,
                  const void *data, int len,
                  blobref_t blobref);

/* Check validity of blobref string.
 */
int blobref_validate (const char *blobref);

/* Check the validity of hash type (by name)
 */
int blobref_validate_hashtype (const char *name);


#endif /* _UTIL_BLOBREF_H */
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
