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
                printk(KERN_DEBUG "Cauchy-debug: [%s:%d] " fmt "\n", \
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
    #define ALIGNED_ACCESSES //therefore inputs must be aligned
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

// Compiler-specific force inline (GCC)
#define FORCE_INLINE inline __attribute__((always_inline))

// Compiler-specific alignment keyword, only matters on ARM
#define ALIGNED __attribute__((aligned(GF_ALIGN_BYTES)))

/// Swap two memory buffers in-place
void gf_memswap(void * __restrict vx, void * __restrict vy, int bytes);


//------------------------------------------------------------------------------

// The context object stores tables required to perform library calculations
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

    // Mul/Div/Inv/Sqr lookup tables
    uint8_t GF_MUL_TABLE[256 * 256];
    uint8_t GF_DIV_TABLE[256 * 256];
    uint8_t GF_INV_TABLE[256];
    uint8_t GF_SQR_TABLE[256];

    // Log/Exp lookup tables
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

//Galois field add
static FORCE_INLINE uint8_t gf_add(uint8_t x, uint8_t y)
{
    return (uint8_t)(x ^ y);
}

// Galois Field multiply
static FORCE_INLINE uint8_t gf_mul(uint8_t x, uint8_t y)
{
    return GFContext.GF_MUL_TABLE[((unsigned)y << 8) + x];
}

//Galois Field divide
static FORCE_INLINE uint8_t gf_div(uint8_t x, uint8_t y)
{
    return GFContext.GF_DIV_TABLE[((unsigned)y << 8) + x];
}

//Galois Field inverse
static FORCE_INLINE uint8_t gf_inv(uint8_t x)
{
    return GFContext.GF_INV_TABLE[x];
}

//Galois field square
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
// block counts must be at most 256
typedef struct cauchy_encoder_params_t {
    int OriginalCount;
    int RecoveryCount;
    int BlockBytes;
} cauchy_encoder_params;

typedef struct cauchy_block_t {
    uint8_t* Block;
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
 * This produces a set of parity blocks from the original data blocks as specified
 * in the parameters structure.
 */
int cauchy_rs_encode(
    cauchy_encoder_params params, // Encoder parameters
    uint8_t** dataBlocks,         // Array of pointers to original blocks
    uint8_t** parityBlocks);      // Array of pointers to output parity blocks

// Encode one block.
// TODO validate input
void cauchy_rs_encode_block(
    cauchy_encoder_params params, // Encoder parameters
    cauchy_block* originals,      // Array of pointers to original blocks
    int recoveryBlockIndex,      // Return value from cauchy_get_recovery_block_index()
    void* recoveryBlock);        // Output recovery block

/*
 * Cauchy Reed-Solomon decode
 *
 * Input is the array of the (potentially damaged) data blocks, the parity
 * blocks that are output by the encoding function, and an array of erasures
 *
 * The length of the erasures array should be equal to the number of erasures
 * Each entry in that array is the index of an erased block in the original 
 * code word.
 */
int cauchy_rs_decode(
    cauchy_encoder_params params, // Encoder parameters
    uint8_t** dataBlocks,         // array of pointers to data blocks
    uint8_t** parityBlocks,       // array of pointers to parity blocks
    uint8_t* erasures,            // array of erasures
    uint8_t num_erasures);        // the number of erasures


#endif
