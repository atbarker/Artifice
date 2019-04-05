/* 
   Austen Barker (2019)
   Based off CM256 by Christopher Taylor.
*/
/*
	Copyright (c) 2015 Christopher A. Taylor.  All rights reserved.

	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:

	* Redistributions of source code must retain the above copyright notice,
	  this list of conditions and the following disclaimer.
	* Redistributions in binary form must reproduce the above copyright notice,
	  this list of conditions and the following disclaimer in the documentation
	  and/or other materials provided with the distribution.
	* Neither the name of CM256 nor the names of its contributors may be
	  used to endorse or promote products derived from this software without
	  specific prior written permission.

	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
	AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
	IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
	ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
	LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
	CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
	SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
	INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
	CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
	ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
	POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef CAUCHY_RS_H
#define CAUCHY_RS_H

#if defined(__KERNEL__)
    #include <linux/string.h>
    #include <linux/types.h>
    #include <linux/slab.h>
    #include <asm/fpu/api.h>
    #define cauchy_malloc(arg) kmalloc(arg, GFP_KERNEL)
#else
    #include <stdlib.h>
    #include <stdint.h>
    #include <stdio.h>
    #define cauchy_malloc(arg) malloc(arg)
#endif


#define DEBUG

//fun with print statements
#if defined(DEBUG)
    #if defined(__KERNEL__)
        #define debug(fmt, ...)                                      \
            ({                                                       \
                printk(KERN_DEBUG "Cauchy-debug: [%s:%d] " fmt, \
                    __func__, __LINE__,                              \
                    ##__VA_ARGS__);                                  \
            })
    #else
        #define debug(...) do { fprintf(stderr, __VA_ARGS__ ); } while (false)
    #endif
#else
    #define debug(...) do { } while (false)
#endif

//typedefs from GCC intrinsic file, helps to define vectors
typedef long long __m128i __attribute__ ((__vector_size__ (16), __may_alias__));
typedef unsigned long long __v2du __attribute__ ((__vector_size__ (16)));
typedef long long __v2di __attribute__ ((__vector_size__ (16)));
typedef char __v16qi __attribute__ ((__vector_size__ (16)));

//AVX typedefs, 256 bit parallel to the above typedefs 
typedef long long __m256i __attribute__ ((__vector_size__ (32), __may_alias__));
typedef unsigned long long __v4du __attribute__ ((__vector_size__ (32)));
typedef long long __v4di __attribute__ ((__vector_size__ (32)));
typedef char __v32qi __attribute__ ((__vector_size__ (32)));

//check for AVX extension
#ifdef __AVX2__
    #define GF_AVX2 /* 256-bit */
    #define M256 __m256i
    #define GF_ALIGN_BYTES 32
#else //if we don't have AVX2 then align to 16 bytes
    #define GF_ALIGN_BYTES 16
#endif

#if defined(ANDROID) || defined(IOS) || defined(LINUX_ARM)
    #define GF_ARM //we are on ARM
    #define ALIGNED_ACCESSES //Inputs must be aligned to GF_ALIGN_BYTES
    #if defined(HAVE_ARM_NEON_H)
        #include <arm_neon.h>
        #define M128 uint8x16_t
        #define GF_NEON
    #else
        #define M128 uint64_t
    #endif
#else //if we don't have ARM or then our 128 bit vector is the __m128i type 
    #define M128 __m128i
#endif

// Compiler-specific force inline keyword
#define FORCE_INLINE inline __attribute__((always_inline))

// Compiler-specific alignment keyword, only matters on ARM
#define ALIGNED __attribute__((aligned(GF_ALIGN_BYTES)))

/// Swap two memory buffers in-place
void gf_memswap(void * __restrict vx, void * __restrict vy, int bytes);


//------------------------------------------------------------------------------
// GF(256) Context

/// The context object stores tables required to perform library calculations
typedef struct{
    struct
    {
        ALIGNED M128 TABLE_LO_Y[256];
        ALIGNED M128 TABLE_HI_Y[256];
    } MM128;
#ifdef GF_AVX2
    struct
    {
        ALIGNED M256 TABLE_LO_Y[256];
        ALIGNED M256 TABLE_HI_Y[256];
    } MM256;
#endif

    // Mul/Div/Inv/Sqr tables
    uint8_t GF_MUL_TABLE[256 * 256];
    uint8_t GF_DIV_TABLE[256 * 256];
    uint8_t GF_INV_TABLE[256];
    uint8_t GF_SQR_TABLE[256];

    // Log/Exp tables
    uint16_t GF_LOG_TABLE[256];
    uint8_t GF_EXP_TABLE[512 * 2 + 1];

    // Polynomial used
    unsigned Polynomial;
}gf_ctx;

//global context
//TODO get rid of the global context, should be generated and passed into functions
extern gf_ctx GFContext;

/**
    Initialize a context, filling in the tables.
    The gf_ctx object must be aligned to 16 byte boundary.
    Example:
       static ALIGNED gf_ctx TheGFContext;
       gf_init(&TheGFContext, 0);  
*/
int gf_init(void);

/// return x + y
static FORCE_INLINE uint8_t gf_add(uint8_t x, uint8_t y)
{
    return (uint8_t)(x ^ y);
}

/// return x * y
/// For repeated multiplication by a constant, it is faster to put the constant in y.
static FORCE_INLINE uint8_t gf_mul(uint8_t x, uint8_t y)
{
    return GFContext.GF_MUL_TABLE[((unsigned)y << 8) + x];
}

/// return x / y
/// Memory-access optimized for constant divisors in y.
static FORCE_INLINE uint8_t gf_div(uint8_t x, uint8_t y)
{
    return GFContext.GF_DIV_TABLE[((unsigned)y << 8) + x];
}

/// return 1 / x
static FORCE_INLINE uint8_t gf_inv(uint8_t x)
{
    return GFContext.GF_INV_TABLE[x];
}

/// return x * x
static FORCE_INLINE uint8_t gf_sqr(uint8_t x)
{
    return GFContext.GF_SQR_TABLE[x];
}

/// Performs "x[] += y[]" bulk memory XOR operation
void gf_add_mem(void * __restrict vx, const void * __restrict vy, int bytes);

/// Performs "z[] += x[] + y[]" bulk memory operation
void gf_add2_mem(void * __restrict vz, const void * __restrict vx, const void * __restrict vy, int bytes);

/// Performs "z[] = x[] + y[]" bulk memory operation
void gf_addset_mem(void * __restrict vz, const void * __restrict vx, const void * __restrict vy, int bytes);

/// Performs "z[] = x[] * y" bulk memory operation
void gf_mul_mem(void * __restrict vz, const void * __restrict vx, uint8_t y, int bytes);

/// Performs "z[] += x[] * y" bulk memory operation
void gf_muladd_mem(void * __restrict vz, uint8_t y, const void * __restrict vx, int bytes);

/// Performs "x[] /= y" bulk memory operation
static FORCE_INLINE void gf_div_mem(void * __restrict vz, const void * __restrict vx, uint8_t y, int bytes)
{
    // Multiply by inverse
    gf_mul_mem(vz, vx, y == 1 ? (uint8_t)1 : GFContext.GF_INV_TABLE[y], bytes);
}

//Initialize the encoder
int cauchy_init(void);

// Encoder parameters
// block counts must be less than 256
typedef struct cauchy_encoder_params_t {
    int OriginalCount;
    int RecoveryCount;
    int BlockBytes; //block size in bytes
} cauchy_encoder_params;

typedef struct cauchy_block_t {
    uint8_t* Block;
    // Block index.
    // For original data, it will be in the range
    //    [0..(originalCount-1)] inclusive.
    // For recovery data, the first one's Index must be originalCount,
    //    and it will be in the range
    //    [originalCount..(originalCount+recoveryCount-1)] inclusive.
    unsigned char Index;
} cauchy_block;


// Compute the value to put in the Index member of cauchy_block
static inline unsigned char cauchy_get_recovery_block_index(cauchy_encoder_params params, int recoveryBlockIndex)
{
    //assert(recoveryBlockIndex >= 0 && recoveryBlockIndex < params.RecoveryCount);
    return (unsigned char)(params.OriginalCount + recoveryBlockIndex);
}
static inline unsigned char cauchy_get_original_block_index(cauchy_encoder_params params, int originalBlockIndex)
{
    //assert(originalBlockIndex >= 0 && originalBlockIndex < params.OriginalCount);
    return (unsigned char)(originalBlockIndex);
}


/*
 * This produces a set of recovery blocks that should be transmitted after the
 * original data blocks.
 *
 * It takes in 'originalCount' equal-sized blocks and produces 'recoveryCount'
 * equally-sized recovery blocks.
 *
 * The input 'originals' array allows more natural usage of the library.
 * The output recovery blocks are stored end-to-end in 'recoveryBlocks'.
 * 'recoveryBlocks' should have recoveryCount * blockBytes bytes available.
 *
 * Precondition: originalCount + recoveryCount <= 256
 *
 * When transmitting the data, the block index of the data should be sent,
 * and the recovery block index is also needed.  The decoder should also
 * be provided with the values of originalCount, recoveryCount and blockBytes.
 *
 * Example wire format:
 * [originalCount(1 byte)] [recoveryCount(1 byte)]
 * [blockIndex(1 byte)] [blockData(blockBytes bytes)]
 *
 * Be careful not to mix blocks from different encoders.
 *
 * It is possible to support variable-length data by including the original
 * data length at the front of each message in 2 bytes, such that when it is
 * recovered after a loss the data length is available in the block data and
 * the remaining bytes of padding can be neglected.
 *
 * Returns 0 on success, and any other code indicates failure.
 */
int cauchy_rs_encode(
    cauchy_encoder_params params, // Encoder parameters
    cauchy_block* originals,      // Array of pointers to original blocks
    void* recoveryBlocks);       // Output recovery blocks end-to-end

// Encode one block.
// TODO validate input
void cauchy_rs_encode_block(
    cauchy_encoder_params params, // Encoder parameters
    cauchy_block* originals,      // Array of pointers to original blocks
    int recoveryBlockIndex,      // Return value from cauchy_get_recovery_block_index()
    void* recoveryBlock);        // Output recovery block

/*
 * Cauchy MDS GF(256) decode
 *
 * This recovers the original data from the recovery data in the provided
 * blocks.  There should be 'originalCount' blocks in the provided array.
 * Recovery will always be possible if that many blocks are received.
 *
 * Provide the same values for 'originalCount', 'recoveryCount', and
 * 'blockBytes' used by the encoder.
 *
 * The block Index should be set to the block index of the original data,
 * as described in the cauchy_block struct comments above.
 *
 * Recovery blocks will be replaced with original data and the Index
 * will be updated to indicate the original block that was recovered.
 *
 * Returns 0 on success, and any other code indicates failure.
 */
int cauchy_rs_decode(
    cauchy_encoder_params params, // Encoder parameters
    cauchy_block* blocks);        // Array of 'originalCount' blocks as described above


#endif
