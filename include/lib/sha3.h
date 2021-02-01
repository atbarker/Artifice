#ifndef SHA3
#define SHA3

#ifndef __KERNEL__
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define sha_malloc(X) malloc(X)
#define sha_calloc(X, Y) calloc(X, Y)
#define sha_free(X) free(X)
#else
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/string.h>
#define sha_malloc(X) kmalloc(X, GFP_KERNEL)
#define sha_calloc(X, Y) kcalloc(X, Y, GFP_KERNEL)
#define sha_free(X) kfree(X)
#endif

void sha3_256(uint8_t*, uint64_t, uint8_t*);

#endif
