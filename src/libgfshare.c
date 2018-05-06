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

//#include "config.h"
#include "libgfshare.h"
#include "libgfshare_tables.h"
#include <linux/slab.h>
//#include <errno.h>
//#include <stdlib.h>
#include <linux/string.h>
//#include <stdio.h>

//#define XMALLOC malloc
//#define XFREE free

struct _gfshare_ctx {
  unsigned int sharecount;
  unsigned int threshold;
  unsigned int size;
  unsigned char* sharenrs;
  unsigned char* buffer;
  unsigned int buffersize;
};


//TODO: fix random
void
gfshare_fill_rand_using_random(unsigned char *buffer,
        unsigned int count)
{
  unsigned int i;
  for( i = 0; i < count; ++i )
    buffer[i] = (random() & 0xff00) >> 8; /* apparently the bottom 8 aren't
                                           * very random but the middles ones
                                           * are
                                           */
}


//TODO: Get random data from other source
void
gfshare_fill_rand_using_dev_urandom( unsigned char *buffer,
        unsigned int count )
{
  /*size_t n;
  FILE *devrandom;

  //devrandom = fopen("/dev/urandom", "rb");
  if (!devrandom) {
    //perror("Unable to read /dev/urandom");
    //abort();
  }
  //n = fread(buffer, 1, count, devrandom);
  if (n < count) {
    //perror("Short read from /dev/urandom");
    //abort();
  }
  //fclose(devrandom);*/
}

gfshare_rand_func_t gfshare_fill_rand = gfshare_fill_rand_using_dev_urandom;
/*
unsigned int
gfshare_file_getlen( FILE* f )
{
  unsigned int len;
  fseek(f, 0, SEEK_END);
  len = ftell(f);
  fseek(f, 0, SEEK_SET);
  return len;
}*/

/* ------------------------------------------------------[ Preparation ]---- */

void
gfshare_generate_sharenrs( unsigned char *sharenrs,
                           unsigned int sharecount )
{
  int i, j;
  for (i = 0; i < sharecount; ++i) {
    unsigned char proposed = (random() & 0xff00) >> 8;
    if (proposed == 0) proposed = 1;
    SHARENR_TRY_AGAIN:
    for (j = 0; j < i; ++j) {
      if (sharenrs[j] == proposed) {
        proposed++;
        if (proposed == 0) proposed = 1;
        goto SHARENR_TRY_AGAIN;
      }
    }
    sharenrs[i] = proposed;
  }
}

static gfshare_ctx *
_gfshare_ctx_init_core( unsigned char *sharenrs,
                        unsigned int sharecount,
                        unsigned char threshold,
                        unsigned int size )
{
  gfshare_ctx *ctx;
  
  ctx = kmalloc( sizeof(struct _gfshare_ctx), GFP_KERNEL);
  if( ctx == NULL )
    return NULL; /* errno should still be set from XMALLOC() */
  
  ctx->sharecount = sharecount;
  ctx->threshold = threshold;
  ctx->size = size;
  ctx->sharenrs = kmalloc(sharecount, GFP_KERNEL);
  
  if( ctx->sharenrs == NULL ) {
    //int saved_errno = errno;
    kfree( ctx );
    //errno = saved_errno;
    return NULL;
  }
  
  memcpy( ctx->sharenrs, sharenrs, sharecount );
  ctx->buffersize = threshold * size;
  ctx->buffer = kmalloc(ctx->buffersize, GFP_KERNEL);
  
  if( ctx->buffer == NULL ) {
    //int saved_errno = errno;
    kfree( ctx->sharenrs );
    kfree( ctx );
    //errno = saved_errno;
    return NULL;
  }
  
  return ctx;
}

/* Initialise a gfshare context for producing shares */
gfshare_ctx *
gfshare_ctx_init_enc( unsigned char* sharenrs,
                      unsigned int sharecount,
                      unsigned char threshold,
                      unsigned int size )
{
  unsigned int i;

  for (i = 0; i < sharecount; i++) {
    if (sharenrs[i] == 0) {
      /* can't have x[i] = 0 - that would just be a copy of the secret, in
       * theory (in fact, due to the way we use exp/log for multiplication and
       * treat log(0) as 0, it ends up as a copy of x[i] = 1) */
      //errno = EINVAL;
      return NULL;
    }
  }

  return _gfshare_ctx_init_core( sharenrs, sharecount, threshold, size );
}

/* Initialise a gfshare context for recombining shares */
gfshare_ctx*
gfshare_ctx_init_dec( unsigned char* sharenrs,
                      unsigned int sharecount,
                      unsigned int size )
{
  gfshare_ctx *ctx = _gfshare_ctx_init_core( sharenrs, sharecount, sharecount, size );
  
  if( ctx != NULL )
    ctx->threshold = 0;
  
  return ctx;
}

/* Free a share context's memory. */
void 
gfshare_ctx_free( gfshare_ctx* ctx )
{
  gfshare_fill_rand( ctx->buffer, ctx->buffersize );
  gfshare_fill_rand( ctx->sharenrs, ctx->sharecount );
  kfree( ctx->sharenrs );
  kfree( ctx->buffer );
  gfshare_fill_rand( (unsigned char*)ctx, sizeof(struct _gfshare_ctx) );
  kfree( ctx );
}

/* --------------------------------------------------------[ Splitting ]---- */

/* Provide a secret to the encoder. (this re-scrambles the coefficients) */
void 
gfshare_ctx_enc_setsecret( gfshare_ctx* ctx,
                           unsigned char* secret)
{
  memcpy( ctx->buffer + ((ctx->threshold-1) * ctx->size),
          secret,
          ctx->size );
  gfshare_fill_rand( ctx->buffer, (ctx->threshold-1) * ctx->size );
}

/* Extract a share from the context. 
 * 'share' must be preallocated and at least 'size' bytes long.
 * 'sharenr' is the index into the 'sharenrs' array of the share you want.
 */
void 
gfshare_ctx_enc_getshare( gfshare_ctx* ctx,
                          unsigned char sharenr,
                          unsigned char* share)
{
  unsigned int pos, coefficient;
  unsigned int ilog = logs[ctx->sharenrs[sharenr]];
  unsigned char *coefficient_ptr = ctx->buffer;
  unsigned char *share_ptr;
  for( pos = 0; pos < ctx->size; ++pos )
    share[pos] = *(coefficient_ptr++);
  for( coefficient = 1; coefficient < ctx->threshold; ++coefficient ) {
    share_ptr = share;
    for( pos = 0; pos < ctx->size; ++pos ) {
      unsigned char share_byte = *share_ptr;
      if( share_byte )
        share_byte = exps[ilog + logs[share_byte]];
      *share_ptr++ = share_byte ^ *coefficient_ptr++;
    }
  }
}

/* Encrypt from an input file pointer to output file pointers */
unsigned int
gfshare_ctx_enc_stream( gfshare_ctx* ctx,
                        FILE *inputfile,
                        FILE **outputfiles)
{
  unsigned char* buffer = malloc( ctx->buffersize );
  if( buffer == NULL ) {
    perror( "malloc" );
    return 1;
  }

  while( !feof(inputfile) ) {
    unsigned int bytes_read = fread( buffer, 1, ctx->buffersize, inputfile );
    if( bytes_read == 0 ) break;
    gfshare_ctx_enc_setsecret( ctx, buffer );
    for( int i = 0; i < ctx->sharecount; ++i ) {
      unsigned int bytes_written;
      gfshare_ctx_enc_getshare( ctx, i, buffer );
      bytes_written = fwrite( buffer, 1, bytes_read, outputfiles[i] );
      if( bytes_read != bytes_written ) {
        fprintf( stderr, "Mismatch during file write to output stream %i.\n", i);
        return 1;
      }
    }
  }
  return 0;
}

/* ----------------------------------------------------[ Recombination ]---- */

/* Inform a recombination context of a change in share indexes */
void 
gfshare_ctx_dec_newshares( gfshare_ctx* ctx,
                           unsigned char* sharenrs)
{
  memcpy( ctx->sharenrs, sharenrs, ctx->sharecount );
}

/* Provide a share context with one of the shares.
 * The 'sharenr' is the index into the 'sharenrs' array
 */
void 
gfshare_ctx_dec_giveshare( gfshare_ctx* ctx,
                           unsigned char sharenr,
                           unsigned char* share )
{
  memcpy( ctx->buffer + (sharenr * ctx->size), share, ctx->size );
}

/* Extract the secret by interpolation of the shares.
 * secretbuf must be allocated and at least 'size' bytes long
 */
void
gfshare_ctx_dec_extract( gfshare_ctx* ctx,
                         unsigned char* secretbuf )
{
  unsigned int i, j;
  unsigned char *secret_ptr, *share_ptr;
  
  for( i = 0; i < ctx->size; ++i )
    secretbuf[i] = 0;
  
  for( i = 0; i < ctx->sharecount; ++i ) {
    /* Compute L(i) as per Lagrange Interpolation */
    unsigned Li_top = 0, Li_bottom = 0;
    
    if( ctx->sharenrs[i] == 0 ) continue; /* this share is not provided. */
    
    for( j = 0; j < ctx->sharecount; ++j ) {
      if( i == j ) continue;
      if( ctx->sharenrs[j] == 0 ) continue; /* skip empty share */
      Li_top += logs[ctx->sharenrs[j]];
      if( Li_top >= 0xff ) Li_top -= 0xff;
      Li_bottom += logs[(ctx->sharenrs[i]) ^ (ctx->sharenrs[j])];
      if( Li_bottom >= 0xff ) Li_bottom -= 0xff;
    }
    if( Li_bottom  > Li_top ) Li_top += 0xff;
    Li_top -= Li_bottom; /* Li_top is now log(L(i)) */
    
    secret_ptr = secretbuf; share_ptr = ctx->buffer + (ctx->size * i);
    for( j = 0; j < ctx->size; ++j ) {
      if( *share_ptr )
        *secret_ptr ^= exps[Li_top + logs[*share_ptr]];
      share_ptr++; secret_ptr++;
    }
  }
}

/* Decrypt from a set of output file pointers to an input file pointer */
unsigned int
gfshare_ctx_dec_stream( gfshare_ctx* ctx,
                        unsigned int filecount,
                        FILE **inputfiles,
                        FILE *outfile)
{
  unsigned char* buffer = malloc( ctx->buffersize );
  if( buffer == NULL ) {
    perror( "malloc" );
    return 1;
  }

  while( !feof(inputfiles[0]) ) {
    unsigned int bytes_read = fread( buffer, 1, ctx->buffersize, inputfiles[0] );
    unsigned int bytes_written;
    gfshare_ctx_dec_giveshare( ctx, 0, buffer );
    for( int i = 1; i < filecount; ++i ) {
      unsigned int bytes_read_2 = fread( buffer, 1, ctx->buffersize, inputfiles[i] );
      if( bytes_read != bytes_read_2 ) {
        fprintf( stderr, "Mismatch during file read.\n");
        return 1;
      }
      gfshare_ctx_dec_giveshare( ctx, i, buffer );
    }
    gfshare_ctx_dec_extract( ctx, buffer );
    bytes_written = fwrite( buffer, 1, bytes_read, outfile );
    if( bytes_written != bytes_read ) {
      fprintf( stderr, "Mismatch during file write.\n");
      return 1;
    }
  }
  return 0;
}
