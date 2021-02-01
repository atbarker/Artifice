#ifndef SPECK
#define SPECK

#ifndef __KERNEL__
#include <stdint.h>
#include <stdlib.h>
#define speck_malloc(X) malloc(X)
#define speck_free(X) free(X)
#else
#include <linux/types.h>
#include <linux/slab.h>
#define speck_malloc(X) kmalloc(X, GFP_KERNEL)
#define speck_free(X) kfree(X)
#endif

void speck_ctr(uint64_t *in, uint64_t *out, size_t pt_length, uint64_t *key, uint64_t *nonce);
void speck_encrypt(uint64_t *in, uint64_t *out, uint64_t *key);
void speck_decrypt(uint64_t *in, uint64_t *out, uint64_t *key);

#endif
