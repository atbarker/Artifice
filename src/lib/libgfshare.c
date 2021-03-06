/*
 * This file is Copyright Daniel Silverstone <dsilvers@digital-scurf.org> 2006
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */

#include "lib/libgfshare.h"
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/string.h>
#include <linux/timekeeping.h>

static unsigned char logs[256] = {
  0x00, 0x00, 0x01, 0x19, 0x02, 0x32, 0x1a, 0xc6,
  0x03, 0xdf, 0x33, 0xee, 0x1b, 0x68, 0xc7, 0x4b,
  0x04, 0x64, 0xe0, 0x0e, 0x34, 0x8d, 0xef, 0x81,
  0x1c, 0xc1, 0x69, 0xf8, 0xc8, 0x08, 0x4c, 0x71,
  0x05, 0x8a, 0x65, 0x2f, 0xe1, 0x24, 0x0f, 0x21,
  0x35, 0x93, 0x8e, 0xda, 0xf0, 0x12, 0x82, 0x45,
  0x1d, 0xb5, 0xc2, 0x7d, 0x6a, 0x27, 0xf9, 0xb9,
  0xc9, 0x9a, 0x09, 0x78, 0x4d, 0xe4, 0x72, 0xa6,
  0x06, 0xbf, 0x8b, 0x62, 0x66, 0xdd, 0x30, 0xfd,
  0xe2, 0x98, 0x25, 0xb3, 0x10, 0x91, 0x22, 0x88,
  0x36, 0xd0, 0x94, 0xce, 0x8f, 0x96, 0xdb, 0xbd,
  0xf1, 0xd2, 0x13, 0x5c, 0x83, 0x38, 0x46, 0x40,
  0x1e, 0x42, 0xb6, 0xa3, 0xc3, 0x48, 0x7e, 0x6e,
  0x6b, 0x3a, 0x28, 0x54, 0xfa, 0x85, 0xba, 0x3d,
  0xca, 0x5e, 0x9b, 0x9f, 0x0a, 0x15, 0x79, 0x2b,
  0x4e, 0xd4, 0xe5, 0xac, 0x73, 0xf3, 0xa7, 0x57,
  0x07, 0x70, 0xc0, 0xf7, 0x8c, 0x80, 0x63, 0x0d,
  0x67, 0x4a, 0xde, 0xed, 0x31, 0xc5, 0xfe, 0x18,
  0xe3, 0xa5, 0x99, 0x77, 0x26, 0xb8, 0xb4, 0x7c,
  0x11, 0x44, 0x92, 0xd9, 0x23, 0x20, 0x89, 0x2e,
  0x37, 0x3f, 0xd1, 0x5b, 0x95, 0xbc, 0xcf, 0xcd,
  0x90, 0x87, 0x97, 0xb2, 0xdc, 0xfc, 0xbe, 0x61,
  0xf2, 0x56, 0xd3, 0xab, 0x14, 0x2a, 0x5d, 0x9e,
  0x84, 0x3c, 0x39, 0x53, 0x47, 0x6d, 0x41, 0xa2,
  0x1f, 0x2d, 0x43, 0xd8, 0xb7, 0x7b, 0xa4, 0x76,
  0xc4, 0x17, 0x49, 0xec, 0x7f, 0x0c, 0x6f, 0xf6,
  0x6c, 0xa1, 0x3b, 0x52, 0x29, 0x9d, 0x55, 0xaa,
  0xfb, 0x60, 0x86, 0xb1, 0xbb, 0xcc, 0x3e, 0x5a,
  0xcb, 0x59, 0x5f, 0xb0, 0x9c, 0xa9, 0xa0, 0x51,
  0x0b, 0xf5, 0x16, 0xeb, 0x7a, 0x75, 0x2c, 0xd7,
  0x4f, 0xae, 0xd5, 0xe9, 0xe6, 0xe7, 0xad, 0xe8,
  0x74, 0xd6, 0xf4, 0xea, 0xa8, 0x50, 0x58, 0xaf };

static unsigned char exps[510] = {
  0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
  0x1d, 0x3a, 0x74, 0xe8, 0xcd, 0x87, 0x13, 0x26,
  0x4c, 0x98, 0x2d, 0x5a, 0xb4, 0x75, 0xea, 0xc9,
  0x8f, 0x03, 0x06, 0x0c, 0x18, 0x30, 0x60, 0xc0,
  0x9d, 0x27, 0x4e, 0x9c, 0x25, 0x4a, 0x94, 0x35,
  0x6a, 0xd4, 0xb5, 0x77, 0xee, 0xc1, 0x9f, 0x23,
  0x46, 0x8c, 0x05, 0x0a, 0x14, 0x28, 0x50, 0xa0,
  0x5d, 0xba, 0x69, 0xd2, 0xb9, 0x6f, 0xde, 0xa1,
  0x5f, 0xbe, 0x61, 0xc2, 0x99, 0x2f, 0x5e, 0xbc,
  0x65, 0xca, 0x89, 0x0f, 0x1e, 0x3c, 0x78, 0xf0,
  0xfd, 0xe7, 0xd3, 0xbb, 0x6b, 0xd6, 0xb1, 0x7f,
  0xfe, 0xe1, 0xdf, 0xa3, 0x5b, 0xb6, 0x71, 0xe2,
  0xd9, 0xaf, 0x43, 0x86, 0x11, 0x22, 0x44, 0x88,
  0x0d, 0x1a, 0x34, 0x68, 0xd0, 0xbd, 0x67, 0xce,
  0x81, 0x1f, 0x3e, 0x7c, 0xf8, 0xed, 0xc7, 0x93,
  0x3b, 0x76, 0xec, 0xc5, 0x97, 0x33, 0x66, 0xcc,
  0x85, 0x17, 0x2e, 0x5c, 0xb8, 0x6d, 0xda, 0xa9,
  0x4f, 0x9e, 0x21, 0x42, 0x84, 0x15, 0x2a, 0x54,
  0xa8, 0x4d, 0x9a, 0x29, 0x52, 0xa4, 0x55, 0xaa,
  0x49, 0x92, 0x39, 0x72, 0xe4, 0xd5, 0xb7, 0x73,
  0xe6, 0xd1, 0xbf, 0x63, 0xc6, 0x91, 0x3f, 0x7e,
  0xfc, 0xe5, 0xd7, 0xb3, 0x7b, 0xf6, 0xf1, 0xff,
  0xe3, 0xdb, 0xab, 0x4b, 0x96, 0x31, 0x62, 0xc4,
  0x95, 0x37, 0x6e, 0xdc, 0xa5, 0x57, 0xae, 0x41,
  0x82, 0x19, 0x32, 0x64, 0xc8, 0x8d, 0x07, 0x0e,
  0x1c, 0x38, 0x70, 0xe0, 0xdd, 0xa7, 0x53, 0xa6,
  0x51, 0xa2, 0x59, 0xb2, 0x79, 0xf2, 0xf9, 0xef,
  0xc3, 0x9b, 0x2b, 0x56, 0xac, 0x45, 0x8a, 0x09,
  0x12, 0x24, 0x48, 0x90, 0x3d, 0x7a, 0xf4, 0xf5,
  0xf7, 0xf3, 0xfb, 0xeb, 0xcb, 0x8b, 0x0b, 0x16,
  0x2c, 0x58, 0xb0, 0x7d, 0xfa, 0xe9, 0xcf, 0x83,
  0x1b, 0x36, 0x6c, 0xd8, 0xad, 0x47, 0x8e, 0x01,
  0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1d,
  0x3a, 0x74, 0xe8, 0xcd, 0x87, 0x13, 0x26, 0x4c,
  0x98, 0x2d, 0x5a, 0xb4, 0x75, 0xea, 0xc9, 0x8f,
  0x03, 0x06, 0x0c, 0x18, 0x30, 0x60, 0xc0, 0x9d,
  0x27, 0x4e, 0x9c, 0x25, 0x4a, 0x94, 0x35, 0x6a,
  0xd4, 0xb5, 0x77, 0xee, 0xc1, 0x9f, 0x23, 0x46,
  0x8c, 0x05, 0x0a, 0x14, 0x28, 0x50, 0xa0, 0x5d,
  0xba, 0x69, 0xd2, 0xb9, 0x6f, 0xde, 0xa1, 0x5f,
  0xbe, 0x61, 0xc2, 0x99, 0x2f, 0x5e, 0xbc, 0x65,
  0xca, 0x89, 0x0f, 0x1e, 0x3c, 0x78, 0xf0, 0xfd,
  0xe7, 0xd3, 0xbb, 0x6b, 0xd6, 0xb1, 0x7f, 0xfe,
  0xe1, 0xdf, 0xa3, 0x5b, 0xb6, 0x71, 0xe2, 0xd9,
  0xaf, 0x43, 0x86, 0x11, 0x22, 0x44, 0x88, 0x0d,
  0x1a, 0x34, 0x68, 0xd0, 0xbd, 0x67, 0xce, 0x81,
  0x1f, 0x3e, 0x7c, 0xf8, 0xed, 0xc7, 0x93, 0x3b,
  0x76, 0xec, 0xc5, 0x97, 0x33, 0x66, 0xcc, 0x85,
  0x17, 0x2e, 0x5c, 0xb8, 0x6d, 0xda, 0xa9, 0x4f,
  0x9e, 0x21, 0x42, 0x84, 0x15, 0x2a, 0x54, 0xa8,
  0x4d, 0x9a, 0x29, 0x52, 0xa4, 0x55, 0xaa, 0x49,
  0x92, 0x39, 0x72, 0xe4, 0xd5, 0xb7, 0x73, 0xe6,
  0xd1, 0xbf, 0x63, 0xc6, 0x91, 0x3f, 0x7e, 0xfc,
  0xe5, 0xd7, 0xb3, 0x7b, 0xf6, 0xf1, 0xff, 0xe3,
  0xdb, 0xab, 0x4b, 0x96, 0x31, 0x62, 0xc4, 0x95,
  0x37, 0x6e, 0xdc, 0xa5, 0x57, 0xae, 0x41, 0x82,
  0x19, 0x32, 0x64, 0xc8, 0x8d, 0x07, 0x0e, 0x1c,
  0x38, 0x70, 0xe0, 0xdd, 0xa7, 0x53, 0xa6, 0x51,
  0xa2, 0x59, 0xb2, 0x79, 0xf2, 0xf9, 0xef, 0xc3,
  0x9b, 0x2b, 0x56, 0xac, 0x45, 0x8a, 0x09, 0x12,
  0x24, 0x48, 0x90, 0x3d, 0x7a, 0xf4, 0xf5, 0xf7,
  0xf3, 0xfb, 0xeb, 0xcb, 0x8b, 0x0b, 0x16, 0x2c,
  0x58, 0xb0, 0x7d, 0xfa, 0xe9, 0xcf, 0x83, 0x1b,
  0x36, 0x6c, 0xd8, 0xad, 0x47, 0x8e };

struct _gfshare_ctx {
  uint32_t sharecount;
  uint32_t threshold;
  uint32_t maxsize;
  uint32_t size;
  uint8_t* sharenrs;
  uint8_t* buffer;
  uint32_t buffersize;
};

//TODO see if there are any faster methods to get good random numbers, RDRAND if x86?
static void _gfshare_fill_rand_using_random_bytes(uint8_t* buffer, size_t count){
    get_random_bytes(buffer, count);
}

#define BLOCK_SIZE 16

#define ROR(x, r) ((x >> r) | (x << (64 - r)))
#define ROL(x, r) ((x << r) | (x >> (64 - r)))
#define R(x, y, k) (x = ROR(x, 8), x += y, x ^= k, y = ROL(y, 3), y ^= x)
#define ROUNDS 32

/**
 * pt: plaintext
 * ct: ciphertext
 * k: key
 * we assume that input arrays are of length 2 so we get 128 bits
 * Should generate the key on the fly, just for simplicity sake
 * Better performance can be had by computing round keys once.
 * This function is obtained from the following paper, https://eprint.iacr.org/2013/404
 */
void speck_encrypt_rng(uint64_t ct[2], uint64_t const pt[2], uint64_t const K[2])
{
    uint64_t y = pt[0], x = pt[1], b = K[0], a = K[1];
    int i = 0;
    R(x, y, b);
    for (i = 0; i < ROUNDS - 1; i++) {
        R(a, b, i);
        R(x, y, b);
    }

    ct[0] = y;
    ct[1] = x;
}

/**
 * output_length: size of the output block
 * output_block: destination for pseudorandom bits
 * seed: a 128 bit random number
 * Generate a block of random bytes given a key (seed) by running speck in counter mode
 * block input should be aligned to 128 bit (16 byte)  boundaries
 * We assume a length of 16 bytes (128 bits) for the seed.
 */
void generate_block_ctr(size_t output_length, uint8_t *output_block, uint8_t *seed){
    uint32_t rounds = output_length/BLOCK_SIZE;
    uint64_t i, ctr[2], key[2], output[2];
    uint64_t j = 0;

    if(output_length % BLOCK_SIZE != 0){
        printk(KERN_INFO "Not aligned to 128 bit boundary %ld", output_length);
    }

    key[0] = ((uint64_t *)seed)[0];
    key[1] = ((uint64_t *)seed)[1];

    ctr[0] = 0;
    ctr[1] = 0;

    for(i = 0; i < rounds; i++){
       speck_encrypt_rng(output, ctr, key);
       ((uint64_t *)output_block)[j + 1] = output[1];
       ((uint64_t *)output_block)[j + 0] = output[0];
       ctr[0]++;
       j += 2;
    }
}

uint64_t * get_seed_64(void){
    static uint64_t random[2];
    get_random_bytes(random, 16);
    return random;
}

static void _gfshare_fill_rand_using_speck(uint8_t* buffer, size_t count){
    uint8_t *key = (uint8_t*)get_seed_64();
    generate_block_ctr(count, buffer, key);
}


gfshare_rand_func_t gfshare_fill_rand = _gfshare_fill_rand_using_speck;


/* ------------------------------------------------------[ Preparation ]---- */

static gfshare_ctx * _gfshare_ctx_init_core(const uint8_t *sharenrs, 
		                            uint32_t sharecount, 
					    uint32_t threshold, 
					    size_t maxsize ) 
{
  gfshare_ctx *ctx;

  /* Size must be nonzero, and 1 <= threshold <= sharecount */
  if( maxsize < 1 || threshold < 1 || threshold > sharecount ) {
    return NULL;
  }
  
  ctx = kmalloc( sizeof(struct _gfshare_ctx), GFP_KERNEL);
  if( ctx == NULL )
    return NULL; /* errno should still be set from XMALLOC() */
  
  ctx->sharecount = sharecount;
  ctx->threshold = threshold;
  ctx->maxsize = maxsize;
  ctx->size = maxsize;
  ctx->sharenrs = kmalloc( sharecount, GFP_KERNEL);
  
  if( ctx->sharenrs == NULL ) {
    kfree( ctx );
    return NULL;
  }
  
  memcpy( ctx->sharenrs, sharenrs, sharecount );
  ctx->buffer = kmalloc( sharecount * maxsize, GFP_KERNEL);
  
  if( ctx->buffer == NULL ) {
    kfree( ctx->sharenrs );
    kfree( ctx );
    return NULL;
  }
  
  return ctx;
}

/* Initialise a gfshare context for producing shares */
gfshare_ctx * gfshare_ctx_init_enc(const uint8_t* sharenrs,
                                   uint32_t sharecount,
                                   uint32_t threshold,
                                   size_t maxsize)
{
  int i;

  for (i = 0; i < sharecount; i++) {
    if (sharenrs[i] == 0) {
      /* can't have x[i] = 0 - that would just be a copy of the secret, in
       * theory (in fact, due to the way we use exp/log for multiplication and
       * treat log(0) as 0, it ends up as a copy of x[i] = 1) */
      return NULL;
    }
  }

  return _gfshare_ctx_init_core( sharenrs, sharecount, threshold, maxsize );
}

/* Initialise a gfshare context for recombining shares */
gfshare_ctx* gfshare_ctx_init_dec(const uint8_t* sharenrs,
                                  uint32_t sharecount,
                                  uint32_t threshold,
                                  size_t maxsize)
{
  return _gfshare_ctx_init_core( sharenrs, sharecount, threshold, maxsize );
}

/* Set the current processing size */
int gfshare_ctx_setsize(gfshare_ctx* ctx, size_t size) {
  if(size < 1 || size >= ctx->maxsize) {
    return 1;
  }
  ctx->size = size;
  return 0;
}

/* Free a share context's memory. */
void gfshare_ctx_free(gfshare_ctx* ctx) {
  gfshare_fill_rand( ctx->buffer, ctx->sharecount * ctx->maxsize );
  _gfshare_fill_rand_using_random_bytes( ctx->sharenrs, ctx->sharecount );
  kfree( ctx->sharenrs );
  kfree( ctx->buffer );
  _gfshare_fill_rand_using_random_bytes( (uint8_t*)ctx, sizeof(struct _gfshare_ctx) );
  kfree( ctx );
}

/* --------------------------------------------------------[ Splitting ]---- */
/* Provide a secret to the encoder. (this re-scrambles the coefficients) */
void gfshare_ctx_enc_setsecret( gfshare_ctx* ctx, const uint8_t* secret) {
  memcpy(ctx->buffer + ((ctx->threshold-1) * ctx->maxsize), secret, ctx->size);
  gfshare_fill_rand(ctx->buffer, (ctx->threshold-1) * ctx->maxsize);
}

/* Extract a share from the context. 
 * 'share' must be preallocated and at least 'size' bytes long.
 * 'sharenr' is the index into the 'sharenrs' array of the share you want.
 */
int gfshare_ctx_enc_getshares(const gfshare_ctx* ctx,
		              const uint8_t* secret,
                              uint8_t** shares)
{
  uint32_t pos, coefficient;
  uint8_t *share_ptr;
  int i;

  memcpy(ctx->buffer + ((ctx->threshold-1) * ctx->maxsize), secret, ctx->size);
  gfshare_fill_rand(ctx->buffer, (ctx->threshold-1) * ctx->maxsize);

  for(i = 0; i < ctx->sharecount; i++) {
    uint32_t ilog = logs[ctx->sharenrs[i]];
    uint8_t *coefficient_ptr = ctx->buffer;

    memcpy(shares[i], coefficient_ptr++, ctx->size);
    coefficient_ptr += ctx->size - 1;

    for(coefficient = 1; coefficient < ctx->threshold; ++coefficient) {
      share_ptr = shares[i];
      coefficient_ptr = ctx->buffer + coefficient * ctx->maxsize;
      for(pos = 0; pos < ctx->size; ++pos) {
        uint8_t share_byte = *share_ptr;
        if(share_byte) {
          share_byte = exps[ilog + logs[share_byte]];
        }
        *share_ptr++ = share_byte ^ *coefficient_ptr++;
      }
    }
  }
  return 0;
}

/* ----------------------------------------------------[ Recombination ]---- */

/* Inform a recombination context of a change in share indexes */
void gfshare_ctx_dec_newshares( gfshare_ctx* ctx, const uint8_t* sharenrs) {
  memcpy(ctx->sharenrs, sharenrs, ctx->sharecount);
}

/* Provide a share context with one of the shares.
 * The 'sharenr' is the index into the 'sharenrs' array
 */
int gfshare_ctx_dec_giveshare(gfshare_ctx* ctx, uint8_t sharenr, const uint8_t* share) {
  if(sharenr >= ctx->sharecount) {
    return 1;
  }
  memcpy(ctx->buffer + (sharenr * ctx->maxsize), share, ctx->size);
  return 0;
}

/* Extract the secret by interpolation of the shares.
 * secretbuf must be allocated and at least 'size' bytes long
 */
void gfshare_ctx_dec_extract(const gfshare_ctx* ctx, uint8_t* secretbuf) {
  uint32_t i, j, n, jn;
  uint8_t *secret_ptr, *share_ptr;

  memset(secretbuf, 0, ctx->size);
  
  for(n = i = 0; n < ctx->threshold && i < ctx->sharecount; ++n, ++i) {
    /* Compute L(i) as per Lagrange Interpolation */
    unsigned Li_top = 0, Li_bottom = 0;
    
    if(ctx->sharenrs[i] == 0) {
      n--;
      continue; /* this share is not provided. */
    }
    
    for(jn = j = 0; jn < ctx->threshold && j < ctx->sharecount; ++jn, ++j) {
      if(i == j) {
	continue;
      }
      if(ctx->sharenrs[j] == 0) {
        jn--;
        continue; /* skip empty share */
      }
      Li_top += logs[ctx->sharenrs[j]];
      Li_bottom += logs[(ctx->sharenrs[i]) ^ (ctx->sharenrs[j])];
    }
    Li_bottom %= 0xff;
    Li_top += 0xff - Li_bottom;
    Li_top %= 0xff;
    /* Li_top is now log(L(i)) */
    
    secret_ptr = secretbuf; share_ptr = ctx->buffer + (ctx->maxsize * i);
    for(j = 0; j < ctx->size; ++j) {
      if(*share_ptr) {
        *secret_ptr ^= exps[Li_top + logs[*share_ptr]];
      }
      share_ptr++; secret_ptr++;
    }
  }
}

void gfshare_ctx_dec_decode(const gfshare_ctx* ctx, uint8_t* sharenrs, uint8_t** shares, uint8_t* secretbuf) {
  uint32_t i, j, n, jn;
  uint8_t *secret_ptr, *share_ptr;

  memcpy(ctx->sharenrs, sharenrs, ctx->sharecount);
  memset(secretbuf, 0, ctx->size);
  for(i = 0; i < ctx->sharecount; i++){
      memcpy(ctx->buffer + (i * ctx->maxsize), shares[i], ctx->size);
  }

  for(n = i = 0; n < ctx->threshold && i < ctx->sharecount; ++n, ++i) {
    /* Compute L(i) as per Lagrange Interpolation */
    unsigned Li_top = 0, Li_bottom = 0;

    if(ctx->sharenrs[i] == 0) {
      n--;
      continue; /* this share is not provided. */
    }

    for(jn = j = 0; jn < ctx->threshold && j < ctx->sharecount; ++jn, ++j) {
      if(i == j) {
        continue;
      }
      if(ctx->sharenrs[j] == 0) {
        jn--;
        continue; /* skip empty share */
      }
      Li_top += logs[ctx->sharenrs[j]];
      Li_bottom += logs[(ctx->sharenrs[i]) ^ (ctx->sharenrs[j])];
    }
    Li_bottom %= 0xff;
    Li_top += 0xff - Li_bottom;
    Li_top %= 0xff;
    /* Li_top is now log(L(i)) */

    secret_ptr = secretbuf; share_ptr = ctx->buffer + (ctx->maxsize * i);
    for(j = 0; j < ctx->size; ++j) {
      if(*share_ptr) {
        *secret_ptr ^= exps[Li_top + logs[*share_ptr]];
      }
      share_ptr++; secret_ptr++;
    }
  }
}

