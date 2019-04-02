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

#include "lib/rs-fast.h"
#include <linux/types.h>
#include <linux/slab.h>

#ifdef LINUX_ARM
#include <unistd.h>
#include <fcntl.h>
#include <elf.h>
#include <linux/auxvec.h>
#endif

#define kTestBufferBytes 63
#define kTestBufferAllocated 64
#define GF256_GEN_POLY_COUNT 16

//------------------------------------------------------------------------------
// Workaround for ARMv7 that doesn't provide vqtbl1_*
// This comes from linux-raid (https://www.spinics.net/lists/raid/msg58403.html)
//
#ifdef GF256_TRY_NEON
#if __ARM_ARCH <= 7 && !defined(__aarch64__)
static GF256_FORCE_INLINE uint8x16_t vqtbl1q_u8(uint8x16_t a, uint8x16_t b) {
    union {
        uint8x16_t    val;
        uint8x8x2_t    pair;
    } __a = { a };

    return vcombine_u8(vtbl2_u8(__a.pair, vget_low_u8(b)),
                       vtbl2_u8(__a.pair, vget_high_u8(b)));
}
#endif
#endif

//------------------------------------------------------------------------------
// Self-Test
//
// This is executed during initialization to make sure the library is working

typedef struct {
    GF256_ALIGNED uint8_t A[kTestBufferAllocated];
    GF256_ALIGNED uint8_t B[kTestBufferAllocated];
    GF256_ALIGNED uint8_t C[kTestBufferAllocated];
}SelfTestBuffersT;

static GF256_ALIGNED SelfTestBuffersT m_SelfTestBuffers;

static int gf256_self_test(void) {
    unsigned i, j;
    uint8_t expectedMul, expectedMulAdd;
    if ((uintptr_t)m_SelfTestBuffers.A % GF256_ALIGN_BYTES != 0) {
        return -1;
    }
    if ((uintptr_t)m_SelfTestBuffers.A % GF256_ALIGN_BYTES != 0) {
        return -1;
    }
    if ((uintptr_t)m_SelfTestBuffers.B % GF256_ALIGN_BYTES != 0) {
        return -1;
    }
    if ((uintptr_t)m_SelfTestBuffers.C % GF256_ALIGN_BYTES != 0) {
        return -1;
    }

    // Check multiplication/division
    for (i = 0; i < 256; ++i) {
        for (j = 0; j < 256; ++j) {
            uint8_t prod = gf256_mul((uint8_t)i, (uint8_t)j);
            if (i != 0 && j != 0) {
                uint8_t div1 = gf256_div(prod, (uint8_t)i);
                uint8_t div2;
                if (div1 != j){
                    //printf("first division failed\n");
                    return -1;
                }
                div2 = gf256_div(prod, (uint8_t)j);
                if (div2 != i){
                    //printk(KERN_INFO "second division failed\n");
                    return -1;
                }
            }
            else if (prod != 0){
                //printk(KERN_INFO "Product is not zero\n");
                return -1;
            }
            if (j == 1 && prod != i){
                //printf("Multiplication error\n");
                return -1;
            }
        }
    }

    // Check for overruns
    m_SelfTestBuffers.A[kTestBufferBytes] = 0x5a;
    m_SelfTestBuffers.B[kTestBufferBytes] = 0x5a;
    m_SelfTestBuffers.C[kTestBufferBytes] = 0x5a;

    // Test gf256_add_mem()
    for (i = 0; i < kTestBufferBytes; ++i) {
        m_SelfTestBuffers.A[i] = 0x1f;
        m_SelfTestBuffers.B[i] = 0xf7;
    }
    gf256_add_mem(m_SelfTestBuffers.A, m_SelfTestBuffers.B, kTestBufferBytes);
    for (i = 0; i < kTestBufferBytes; ++i) {
        if (m_SelfTestBuffers.A[i] != (0x1f ^ 0xf7)){
	        //printf("addition failure\n");
            return -1;
        }
	}

    // Test gf256_add2_mem()
    for (i = 0; i < kTestBufferBytes; ++i) {
        m_SelfTestBuffers.A[i] = 0x1f;
        m_SelfTestBuffers.B[i] = 0xf7;
        m_SelfTestBuffers.C[i] = 0x71;
    }
    gf256_add2_mem(m_SelfTestBuffers.A, m_SelfTestBuffers.B, m_SelfTestBuffers.C, kTestBufferBytes);
    for (i = 0; i < kTestBufferBytes; ++i) {
        if (m_SelfTestBuffers.A[i] != (0x1f ^ 0xf7 ^ 0x71)) {
            return -1;
        }
    }

    // Test gf256_addset_mem()
    for (i = 0; i < kTestBufferBytes; ++i) {
        m_SelfTestBuffers.A[i] = 0x55;
        m_SelfTestBuffers.B[i] = 0xaa;
        m_SelfTestBuffers.C[i] = 0x6c;
    }
    gf256_addset_mem(m_SelfTestBuffers.A, m_SelfTestBuffers.B, m_SelfTestBuffers.C, kTestBufferBytes);
    for (i = 0; i < kTestBufferBytes; ++i) {
        if (m_SelfTestBuffers.A[i] != (0xaa ^ 0x6c)) {
            return -1;
        }
    }
    // Test gf256_muladd_mem()
    for (i = 0; i < kTestBufferBytes; ++i) {
        m_SelfTestBuffers.A[i] = 0xff;
        m_SelfTestBuffers.B[i] = 0xaa;
    }
    expectedMulAdd = gf256_mul(0xaa, 0x6c);
    gf256_muladd_mem(m_SelfTestBuffers.A, 0x6c, m_SelfTestBuffers.B, kTestBufferBytes);
    for (i = 0; i < kTestBufferBytes; ++i) {
        if (m_SelfTestBuffers.A[i] != (expectedMulAdd ^ 0xff)) {
            return -1;
        }
    }

    // Test gf256_mul_mem()
    for (i = 0; i < kTestBufferBytes; ++i) {
        m_SelfTestBuffers.A[i] = 0xff;
        m_SelfTestBuffers.B[i] = 0x55;
    }
    expectedMul = gf256_mul(0xa2, 0x55);
    gf256_mul_mem(m_SelfTestBuffers.A, m_SelfTestBuffers.B, 0xa2, kTestBufferBytes);
    for (i = 0; i < kTestBufferBytes; ++i) {
        if (m_SelfTestBuffers.A[i] != expectedMul) {
            return -1;
        }
    }

    if (m_SelfTestBuffers.A[kTestBufferBytes] != 0x5a) {
        return -1;
    }
    if (m_SelfTestBuffers.B[kTestBufferBytes] != 0x5a) {
        return -1;
    }
    if (m_SelfTestBuffers.C[kTestBufferBytes] != 0x5a) {
        return -1;
    }

    printk(KERN_INFO "Self test passed\n");
    return 0;
}


//------------------------------------------------------------------------------
// Runtime CPU Architecture Check
//
// Feature checks stolen shamelessly from
// https://github.com/jedisct1/libsodium/blob/master/src/libsodium/sodium/runtime.c

#if defined(HAVE_ANDROID_GETCPUFEATURES)
#include <cpu-features.h>
#endif

#if defined(GF256_TRY_NEON)
# if defined(IOS) && defined(__ARM_NEON__)
// Requires iPhone 5S or newer
static const bool CpuHasNeon = true;
static const bool CpuHasNeon64 = true;
# else // ANDROID or LINUX_ARM
#  if defined(__aarch64__)
static bool CpuHasNeon = true;      // if AARCH64, then we have NEON for sure...
static bool CpuHasNeon64 = true;    // And we have ASIMD
#  else
static bool CpuHasNeon = false;     // if not, then we have to check at runtime.
static bool CpuHasNeon64 = false;   // And we don't have ASIMD
#  endif
# endif
#endif

#if !defined(GF256_TARGET_MOBILE)

#ifdef _MSC_VER
    #include <intrin.h> // __cpuid
    #pragma warning(disable: 4752) // found Intel(R) Advanced Vector Extensions; consider using /arch:AVX
#endif

#ifdef GF256_TRY_AVX2
static bool CpuHasAVX2 = false;
#endif
static bool CpuHasSSSE3 = false;

#define CPUID_EBX_AVX2    0x00000020
#define CPUID_ECX_SSSE3   0x00000200

static void _cpuid(unsigned int cpu_info[4U], const unsigned int cpu_info_type)
{
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_AMD64) || defined(_M_IX86))
    __cpuid((int *) cpu_info, cpu_info_type);
#else //if defined(HAVE_CPUID)
    cpu_info[0] = cpu_info[1] = cpu_info[2] = cpu_info[3] = 0;
# ifdef __i386__
    __asm__ __volatile__ ("pushfl; pushfl; "
                          "popl %0; "
                          "movl %0, %1; xorl %2, %0; "
                          "pushl %0; "
                          "popfl; pushfl; popl %0; popfl" :
                          "=&r" (cpu_info[0]), "=&r" (cpu_info[1]) :
                          "i" (0x200000));
    if (((cpu_info[0] ^ cpu_info[1]) & 0x200000) == 0) {
        return; /* LCOV_EXCL_LINE */
    }
# endif
# ifdef __i386__
    __asm__ __volatile__ ("xchgl %%ebx, %k1; cpuid; xchgl %%ebx, %k1" :
                          "=a" (cpu_info[0]), "=&r" (cpu_info[1]),
                          "=c" (cpu_info[2]), "=d" (cpu_info[3]) :
                          "0" (cpu_info_type), "2" (0U));
# elif defined(__x86_64__)
    __asm__ __volatile__ ("xchgq %%rbx, %q1; cpuid; xchgq %%rbx, %q1" :
                          "=a" (cpu_info[0]), "=&r" (cpu_info[1]),
                          "=c" (cpu_info[2]), "=d" (cpu_info[3]) :
                          "0" (cpu_info_type), "2" (0U));
# else
    __asm__ __volatile__ ("cpuid" :
                          "=a" (cpu_info[0]), "=b" (cpu_info[1]),
                          "=c" (cpu_info[2]), "=d" (cpu_info[3]) :
                          "0" (cpu_info_type), "2" (0U));
# endif
#endif
}

#else
#if defined(LINUX_ARM)
static void checkLinuxARMNeonCapabilities( bool& cpuHasNeon ) {
    auto cpufile = open("/proc/self/auxv", O_RDONLY);
    Elf32_auxv_t auxv;
    if (cpufile >= 0) {
        const auto size_auxv_t = sizeof(Elf32_auxv_t);
        while (read(cpufile, &auxv, size_auxv_t) == size_auxv_t) {
            if (auxv.a_type == AT_HWCAP) {
                cpuHasNeon = (auxv.a_un.a_val & 4096) != 0;
                break;
            }
        }
        close(cpufile);
    }else {
        cpuHasNeon = false;
    }
}
#endif
#endif // defined(GF256_TARGET_MOBILE)

static void gf256_architecture_init(void) {
    unsigned int cpu_info[4];
#if defined(GF256_TRY_NEON)

    // Check for NEON support on Android platform
#if defined(HAVE_ANDROID_GETCPUFEATURES)
    AndroidCpuFamily family = android_getCpuFamily();
    if (family == ANDROID_CPU_FAMILY_ARM) {
        if (android_getCpuFeatures() & ANDROID_CPU_ARM_FEATURE_NEON){
            CpuHasNeon = true;
        }
    }
    else if (family == ANDROID_CPU_FAMILY_ARM64) {
        CpuHasNeon = true;
        if (android_getCpuFeatures() & ANDROID_CPU_ARM64_FEATURE_ASIMD) {
            CpuHasNeon64 = true;
        }
    }
#endif

#if defined(LINUX_ARM)
    // Check for NEON support on other ARM/Linux platforms
    checkLinuxARMNeonCapabilities(CpuHasNeon);
#endif

#endif //GF256_TRY_NEON

#if !defined(GF256_TARGET_MOBILE)

    _cpuid(cpu_info, 1);
    CpuHasSSSE3 = ((cpu_info[2] & CPUID_ECX_SSSE3) != 0);

#if defined(GF256_TRY_AVX2)
    _cpuid(cpu_info, 7);
    CpuHasAVX2 = ((cpu_info[1] & CPUID_EBX_AVX2) != 0);
#endif // GF256_TRY_AVX2

    // When AVX2 and SSSE3 are unavailable, Siamese takes 4x longer to decode
    // and 2.6x longer to encode.  Encoding requires a lot more simple XOR ops
    // so it is still pretty fast.  Decoding is usually really quick because
    // average loss rates are low, but when needed it requires a lot more
    // GF multiplies requiring table lookups which is slower.

#endif // GF256_TARGET_MOBILE
}


//------------------------------------------------------------------------------
// Context Object

// Context object for GF(2^^8) math
GF256_ALIGNED gf256_ctx GF256Ctx;
static bool Initialized = false;


//------------------------------------------------------------------------------
// Generator Polynomial

// There are only 16 irreducible polynomials for GF(2^^8)
static const uint8_t GF256_GEN_POLY[GF256_GEN_POLY_COUNT] = {
    0x8e, 0x95, 0x96, 0xa6, 0xaf, 0xb1, 0xb2, 0xb4,
    0xb8, 0xc3, 0xc6, 0xd4, 0xe1, 0xe7, 0xf3, 0xfa
};

static const int kDefaultPolynomialIndex = 3;

// Select which polynomial to use
static void gf256_poly_init(int polynomialIndex) {
    if (polynomialIndex < 0 || polynomialIndex >= GF256_GEN_POLY_COUNT) {
        polynomialIndex = kDefaultPolynomialIndex;
    }

    GF256Ctx.Polynomial = (GF256_GEN_POLY[polynomialIndex] << 1) | 1;
}


//------------------------------------------------------------------------------
// Exponential and Log Tables

// Construct EXP and LOG tables from polynomial
static void gf256_explog_init(void) {
    unsigned poly = GF256Ctx.Polynomial;
    uint8_t* exptab = GF256Ctx.GF256_EXP_TABLE;
    uint16_t* logtab = GF256Ctx.GF256_LOG_TABLE;
    unsigned jj;

    logtab[0] = 512;
    exptab[0] = 1;
    for (jj = 1; jj < 255; ++jj) {
        unsigned next = (unsigned)exptab[jj - 1] * 2;
        if (next >= 256) {
            next ^= poly;
        }

        exptab[jj] = (uint8_t)( next );
        logtab[exptab[jj]] = (uint16_t)( jj );
    }
    exptab[255] = exptab[0];
    logtab[exptab[255]] = 255;
    for (jj = 256; jj < 2 * 255; ++jj) {
        exptab[jj] = exptab[jj % 255];
    }
    exptab[2 * 255] = 1;
    for (jj = 2 * 255 + 1; jj < 4 * 255; ++jj) {
        exptab[jj] = 0;
    }
}


//------------------------------------------------------------------------------
// Multiply and Divide Tables

// Initialize MUL and DIV tables using LOG and EXP tables
static void gf256_muldiv_init(void) {
    // Allocate table memory 65KB x 2
    uint8_t* m = GF256Ctx.GF256_MUL_TABLE;
    uint8_t* d = GF256Ctx.GF256_DIV_TABLE;
    int x, y;

    // Unroll y = 0 subtable
    for (x = 0; x < 256; ++x) {
        m[x] = d[x] = 0;
    }

    // For each other y value:
    for (y = 1; y < 256; ++y) {
        // Calculate log(y) for mult and 255 - log(y) for div
        const uint8_t log_y = (uint8_t)(GF256Ctx.GF256_LOG_TABLE[y]);
        const uint8_t log_yn = 255 - log_y;

        // Next subtable
        m += 256, d += 256;

        // Unroll x = 0
        m[0] = 0, d[0] = 0;

        // Calculate x * y, x / y
        for (x = 1; x < 256; ++x) {
            uint16_t log_x = GF256Ctx.GF256_LOG_TABLE[x];

            m[x] = GF256Ctx.GF256_EXP_TABLE[log_x + log_y];
            d[x] = GF256Ctx.GF256_EXP_TABLE[log_x + log_yn];
        }
    }
}


//------------------------------------------------------------------------------
// Inverse Table

// Initialize INV table using DIV table
static void gf256_inv_init(void) {
    int x;
    for (x = 0; x < 256; ++x) {
        GF256Ctx.GF256_INV_TABLE[x] = gf256_div(1, (uint8_t)(x));
    }
}


//------------------------------------------------------------------------------
// Square Table

// Initialize SQR table using MUL table
static void gf256_sqr_init(void) {
    int x;
    for (x = 0; x < 256; ++x) {
        GF256Ctx.GF256_SQR_TABLE[x] = gf256_mul((uint8_t)(x), (uint8_t)(x));
    }
}


//------------------------------------------------------------------------------
// Multiply and Add Memory Tables

/*
    Fast algorithm to compute m[1..8] = a[1..8] * b in GF(256)
    using SSE3 SIMD instruction set:

    Consider z = x * y in GF(256).
    This operation can be performed bit-by-bit.  Usefully, the partial product
    of each bit is combined linearly with the rest.  This means that the 8-bit
    number x can be split into its high and low 4 bits, and partial products
    can be formed from each half.  Then the halves can be linearly combined:

        z = x[0..3] * y + x[4..7] * y

    The multiplication of each half can be done efficiently via table lookups,
    and the addition in GF(256) is XOR.  There must be two tables that map 16
    input elements for the low or high 4 bits of x to the two partial products.
    Each value for y has a different set of two tables:

        z = TABLE_LO_y(x[0..3]) xor TABLE_HI_y(x[4..7])

    This means that we need 16 * 2 * 256 = 8192 bytes for precomputed tables.

    Computing z[] = x[] * y can be performed 16 bytes at a time by using the
    128-bit register operations supported by modern processors.

    This is efficiently realized in SSE3 using the _mm_shuffle_epi8() function
    provided by Visual Studio 2010 or newer in <tmmintrin.h>.  This function
    uses the low bits to do a table lookup on each byte.  Unfortunately the
    high bit of each mask byte has the special feature that it clears the
    output byte when it is set, so we need to make sure it's cleared by masking
    off the high bit of each byte before using it:

        clr_mask = _mm_set1_epi8(0x0f) = 0x0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f

    For the low half of the partial product, clear the high bit of each byte
    and perform the table lookup:

        p_lo = _mm_and_si128(x, clr_mask)
        p_lo = _mm_shuffle_epi8(p_lo, TABLE_LO_y)

    For the high half of the partial product, shift the high 4 bits of each
    byte into the low 4 bits and clear the high bit of each byte, and then
    perform the table lookup:

        p_hi = _mm_srli_epi64(x, 4)
        p_hi = _mm_and_si128(p_hi, clr_mask)
        p_hi = _mm_shuffle_epi8(p_hi, TABLE_HI_y)

    Finally add the two partial products to form the product, recalling that
    addition is XOR in a Galois field:

        result = _mm_xor_si128(p_lo, p_hi)

    This crunches 16 bytes of x at a time, and the result can be stored in z.
*/

/*
    Intrinsic reference:

    SSE3, VS2010+, tmmintrin.h:

    GF256_M128 _mm_shuffle_epi8(GF256_M128 a, GF256_M128 mask);
        Emits the Supplemental Streaming SIMD Extensions 3 (SSSE3) instruction pshufb. This instruction shuffles 16-byte parameters from a 128-bit parameter.

        Pseudo-code for PSHUFB (with 128 bit operands):

            for i = 0 to 15 {
                 if (SRC[(i * 8)+7] = 1 ) then
                      DEST[(i*8)+7..(i*8)+0] <- 0;
                  else
                      index[3..0] <- SRC[(i*8)+3 .. (i*8)+0];
                      DEST[(i*8)+7..(i*8)+0] <- DEST[(index*8+7)..(index*8+0)];
                 endif
            }

    SSE2, VS2008+, emmintrin.h:

    GF256_M128 _mm_slli_epi64 (GF256_M128 a, int count);
        Shifts the 2 signed or unsigned 64-bit integers in a left by count bits while shifting in zeros.
    GF256_M128 _mm_srli_epi64 (GF256_M128 a, int count);
        Shifts the 2 signed or unsigned 64-bit integers in a right by count bits while shifting in zeros.
    GF256_M128 _mm_set1_epi8 (char b);
        Sets the 16 signed 8-bit integer values to b.
    GF256_M128 _mm_and_si128 (GF256_M128 a, GF256_M128 b);
        Computes the bitwise AND of the 128-bit value in a and the 128-bit value in b.
    GF256_M128 _mm_xor_si128 ( GF256_M128 a, GF256_M128 b);
        Computes the bitwise XOR of the 128-bit value in a and the 128-bit value in b.
*/

// Initialize the multiplication tables using gf256_mul()
static void gf256_mul_mem_init(void) {
    // Reuse aligned self test buffers to load table data
    uint8_t* lo = m_SelfTestBuffers.A;
    uint8_t* hi = m_SelfTestBuffers.B;
    int y;
    unsigned char x;

    for (y = 0; y < 256; ++y) {
        GF256_M128 table_lo, table_hi;
        // TABLE_LO_Y maps 0..15 to 8-bit partial product based on y.
        for (x = 0; x < 16; ++x) {
            lo[x] = gf256_mul(x, (uint8_t)( y ));
            hi[x] = gf256_mul(x << 4, (uint8_t)( y ));
        }

#if defined(GF256_TRY_NEON)
        if (CpuHasNeon) {
            GF256Ctx.MM128.TABLE_LO_Y[y] = vld1q_u8(lo);
            GF256Ctx.MM128.TABLE_HI_Y[y] = vld1q_u8(hi);
        }
#elif !defined(GF256_TARGET_MOBILE)
        kernel_fpu_begin();
        table_lo = _mm_loadu_si128((GF256_M128*)lo);
        table_hi = _mm_loadu_si128((GF256_M128*)hi);
        _mm_storeu_si128(GF256Ctx.MM128.TABLE_LO_Y + y, table_lo);
        _mm_storeu_si128(GF256Ctx.MM128.TABLE_HI_Y + y, table_hi);
        kernel_fpu_end();
# ifdef GF256_TRY_AVX2
        if (CpuHasAVX2) {
            kernel_fpu_begin();
            const GF256_M256 table_lo2 = _mm256_broadcastsi128_si256(table_lo);
            const GF256_M256 table_hi2 = _mm256_broadcastsi128_si256(table_hi);
            _mm256_storeu_si256(GF256Ctx.MM256.TABLE_LO_Y + y, table_lo2);
            _mm256_storeu_si256(GF256Ctx.MM256.TABLE_HI_Y + y, table_hi2);
            kernel_fpu_end();
        }
# endif // GF256_TRY_AVX2
#endif // GF256_TARGET_MOBILE
    }
}


//------------------------------------------------------------------------------
// Initialization

static unsigned char kLittleEndianTestData[4] = { 4, 3, 2, 1 };

typedef union {
    uint32_t IntValue;
    char CharArray[4];
}UnionType;

static bool IsLittleEndian(void) {
    unsigned i;
    UnionType type;
    for (i = 0; i < 4; ++i)
        type.CharArray[i] = kLittleEndianTestData[i];
    return 0x01020304 == type.IntValue;
}

extern int gf256_init_(int version) {
    if (version != GF256_VERSION){
        printk(KERN_INFO "something wrong\n");
        return -1; // User's header does not match library version.
    }

    // Avoid multiple initialization
    if (Initialized) {
        printk(KERN_INFO "Already initialized\n");
        return 0;
    }
    Initialized = true;

    if (!IsLittleEndian()) {
        printk(KERN_INFO "is it big endian?\n");
        return -2; // Architecture is not supported (code won't work without mods).
    }

    gf256_architecture_init();
    gf256_poly_init(kDefaultPolynomialIndex);
    gf256_explog_init();
    gf256_muldiv_init();
    gf256_inv_init();
    gf256_sqr_init();
    gf256_mul_mem_init();

    if (gf256_self_test()) {
        printk(KERN_INFO "failed self test\n");
        return -3; // Self-test failed (perhaps untested configuration)
    }

    return 0;
}


//------------------------------------------------------------------------------
// Operations

extern void gf256_add_mem(void * GF256_RESTRICT vx,
                              const void * GF256_RESTRICT vy, int bytes) {
    GF256_M128 * GF256_RESTRICT x16 = (GF256_M128 *)(vx);
    const GF256_M128 * GF256_RESTRICT y16 = (const GF256_M128 *)(vy);
    
    int8_t * GF256_RESTRICT x1;
    uint8_t * GF256_RESTRICT y1;
    int offset, eight, four;
#if defined(GF256_TARGET_MOBILE)
# if defined(GF256_TRY_NEON)
    // Handle multiples of 64 bytes
    if (CpuHasNeon) {
        while (bytes >= 64) {
            GF256_M128 x0 = vld1q_u8((uint8_t*) x16);
            GF256_M128 x1 = vld1q_u8((uint8_t*)(x16 + 1) );
            GF256_M128 x2 = vld1q_u8((uint8_t*)(x16 + 2) );
            GF256_M128 x3 = vld1q_u8((uint8_t*)(x16 + 3) );
            GF256_M128 y0 = vld1q_u8((uint8_t*)y16);
            GF256_M128 y1 = vld1q_u8((uint8_t*)(y16 + 1));
            GF256_M128 y2 = vld1q_u8((uint8_t*)(y16 + 2));
            GF256_M128 y3 = vld1q_u8((uint8_t*)(y16 + 3));

            vst1q_u8((uint8_t*)x16,     veorq_u8(x0, y0));
            vst1q_u8((uint8_t*)(x16 + 1), veorq_u8(x1, y1));
            vst1q_u8((uint8_t*)(x16 + 2), veorq_u8(x2, y2));
            vst1q_u8((uint8_t*)(x16 + 3), veorq_u8(x3, y3));

            bytes -= 64, x16 += 4, y16 += 4;
        }

        // Handle multiples of 16 bytes
        while (bytes >= 16) {
            GF256_M128 x0 = vld1q_u8((uint8_t*)x16);
            GF256_M128 y0 = vld1q_u8((uint8_t*)y16);

            vst1q_u8((uint8_t*)x16, veorq_u8(x0, y0));

            bytes -= 16, ++x16, ++y16;
        }
    }
    else
# endif // GF256_TRY_NEON
    {
        unsigned ii;
        uint64_t * GF256_RESTRICT x8 = (uint64_t *)(x16);
        const uint64_t * GF256_RESTRICT y8 = (const uint64_t *)(y16);

        const unsigned count = (unsigned)bytes / 8;
        for (ii = 0; ii < count; ++ii)
            x8[ii] ^= y8[ii];

        x16 = (GF256_M128 *)(x8 + count);
        y16 = (const GF256_M128 *)(y8 + count);

        bytes -= (count * 8);
    }
#else // GF256_TARGET_MOBILE
# if defined(GF256_TRY_AVX2)
    if (CpuHasAVX2) {
        GF256_M256 * GF256_RESTRICT x32 = (GF256_M256 *)(x16);
        const GF256_M256 * GF256_RESTRICT y32 = (const GF256_M256 *)(y16);

        while (bytes >= 128) {
            GF256_M256 x0, x1, x2, x3, y0, y1, y2, y3;
            kernel_fpu_begin();
            x0 = _mm256_loadu_si256(x32);
            y0 = _mm256_loadu_si256(y32);
            x0 = _mm256_xor_si256(x0, y0);
            x1 = _mm256_loadu_si256(x32 + 1);
            y1 = _mm256_loadu_si256(y32 + 1);
            x1 = _mm256_xor_si256(x1, y1);
            x2 = _mm256_loadu_si256(x32 + 2);
            y2 = _mm256_loadu_si256(y32 + 2);
            x2 = _mm256_xor_si256(x2, y2);
            x3 = _mm256_loadu_si256(x32 + 3);
            y3 = _mm256_loadu_si256(y32 + 3);
            x3 = _mm256_xor_si256(x3, y3);

            _mm256_storeu_si256(x32, x0);
            _mm256_storeu_si256(x32 + 1, x1);
            _mm256_storeu_si256(x32 + 2, x2);
            _mm256_storeu_si256(x32 + 3, x3);

            kernel_fpu_end();
            bytes -= 128, x32 += 4, y32 += 4;
        }

        // Handle multiples of 32 bytes
        while (bytes >= 32) {
            // x[i] = x[i] xor y[i]
            kernel_fpu_begin();
            _mm256_storeu_si256(x32,
                _mm256_xor_si256(
                    _mm256_loadu_si256(x32),
                    _mm256_loadu_si256(y32)));
            kernel_fpu_end();

            bytes -= 32, ++x32, ++y32;
        }

        x16 = (GF256_M128 *)(x32);
        y16 = (const GF256_M128 *)(y32);
    }
    else
# endif // GF256_TRY_AVX2
    {
        while (bytes >= 64) {
            GF256_M128 x0, y0, x1, x2, x3, y1, y2, y3;
            kernel_fpu_begin();
            x0 = _mm_loadu_si128(x16);
            y0 = _mm_loadu_si128(y16);
            x0 = _mm_xor_si128(x0, y0);
            x1 = _mm_loadu_si128(x16 + 1);
            y1 = _mm_loadu_si128(y16 + 1);
            x1 = _mm_xor_si128(x1, y1);
            x2 = _mm_loadu_si128(x16 + 2);
            y2 = _mm_loadu_si128(y16 + 2);
            x2 = _mm_xor_si128(x2, y2);
            x3 = _mm_loadu_si128(x16 + 3);
            y3 = _mm_loadu_si128(y16 + 3);
            x3 = _mm_xor_si128(x3, y3);

            _mm_storeu_si128(x16, x0);
            _mm_storeu_si128(x16 + 1, x1);
            _mm_storeu_si128(x16 + 2, x2);
            _mm_storeu_si128(x16 + 3, x3);

            kernel_fpu_end();
            bytes -= 64, x16 += 4, y16 += 4;
        }
    }
#endif // GF256_TARGET_MOBILE

#if !defined(GF256_TARGET_MOBILE)
    // Handle multiples of 16 bytes
    while (bytes >= 16) {
        // x[i] = x[i] xor y[i]
        kernel_fpu_begin();
        _mm_storeu_si128(x16,
            _mm_xor_si128(
                _mm_loadu_si128(x16),
                _mm_loadu_si128(y16)));

        kernel_fpu_end();
        bytes -= 16, ++x16, ++y16;
    }
#endif

    x1 = (uint8_t *)(x16);
    y1 = (uint8_t *)(y16);

    // Handle a block of 8 bytes
    eight = bytes & 8;
    if (eight) {
        uint64_t * GF256_RESTRICT x8 = (uint64_t *)(x1);
        const uint64_t * GF256_RESTRICT y8 = (const uint64_t *)(y1);
        *x8 ^= *y8;
    }

    // Handle a block of 4 bytes
    four = bytes & 4;
    if (four) {
        uint32_t * GF256_RESTRICT x4 = (uint32_t *)(x1 + eight);
        const uint32_t * GF256_RESTRICT y4 = (const uint32_t *)(y1 + eight);
        *x4 ^= *y4;
    }

    // Handle final bytes
    offset = eight + four;
    switch (bytes & 3) {
        case 3: x1[offset + 2] ^= y1[offset + 2];
        case 2: x1[offset + 1] ^= y1[offset + 1];
        case 1: x1[offset] ^= y1[offset];
        default:
            break;
    }
}

extern void gf256_add2_mem(void * GF256_RESTRICT vz, const void * GF256_RESTRICT vx,
                               const void * GF256_RESTRICT vy, int bytes)
{
    GF256_M128 * GF256_RESTRICT z16 = (GF256_M128*)(vz);
    const GF256_M128 * GF256_RESTRICT x16 = (const GF256_M128*)(vx);
    const GF256_M128 * GF256_RESTRICT y16 = (const GF256_M128*)(vy);

    uint8_t * GF256_RESTRICT z1;
    uint8_t * GF256_RESTRICT x1;
    uint8_t * GF256_RESTRICT y1;
    int eight, four, offset;

#if defined(GF256_TARGET_MOBILE)
# if defined(GF256_TRY_NEON)
    // Handle multiples of 64 bytes
    if (CpuHasNeon) {
        // Handle multiples of 16 bytes
        while (bytes >= 16) {
            // z[i] = z[i] xor x[i] xor y[i]
            vst1q_u8((uint8_t*)z16,
                veorq_u8(
                    vld1q_u8((uint8_t*)z16),
                    veorq_u8(
                        vld1q_u8((uint8_t*)x16),
                        vld1q_u8((uint8_t*)y16))));

            bytes -= 16, ++x16, ++y16, ++z16;
        }
    }
    else
# endif // GF256_TRY_NEON
    {
	unsigned ii;
        uint64_t * GF256_RESTRICT z8 = (uint64_t *)(z16);
        const uint64_t * GF256_RESTRICT x8 = (const uint64_t *)(x16);
        const uint64_t * GF256_RESTRICT y8 = (const uint64_t *)(y16);

        const unsigned count = (unsigned)bytes / 8;
        for (ii = 0; ii < count; ++ii)
            z8[ii] ^= x8[ii] ^ y8[ii];

        z16 = (GF256_M128 *)(z8 + count);
        x16 = (const GF256_M128 *)(x8 + count);
        y16 = (const GF256_M128 *)(y8 + count);

        bytes -= (count * 8);
    }
#else // GF256_TARGET_MOBILE
# if defined(GF256_TRY_AVX2)
    if (CpuHasAVX2) {
	unsigned i;
        GF256_M256 * GF256_RESTRICT z32 = (GF256_M256 *)(z16);
        const GF256_M256 * GF256_RESTRICT x32 = (const GF256_M256 *)(x16);
        const GF256_M256 * GF256_RESTRICT y32 = (const GF256_M256 *)(y16);

        const unsigned count = bytes / 32;
        for (i = 0; i < count; ++i) {
            kernel_fpu_begin();
            _mm256_storeu_si256(z32 + i,
                _mm256_xor_si256(
                    _mm256_loadu_si256(z32 + i),
                    _mm256_xor_si256(
                        _mm256_loadu_si256(x32 + i),
                        _mm256_loadu_si256(y32 + i))));
            kernel_fpu_end();
        }

        bytes -= count * 32;
        z16 = (GF256_M128 *)(z32 + count);
        x16 = (const GF256_M128 *)(x32 + count);
        y16 = (const GF256_M128 *)(y32 + count);
    }
# endif // GF256_TRY_AVX2

    // Handle multiples of 16 bytes
    while (bytes >= 16) {
        // z[i] = z[i] xor x[i] xor y[i]
        kernel_fpu_begin();
        _mm_storeu_si128(z16,
            _mm_xor_si128(
                _mm_loadu_si128(z16),
                _mm_xor_si128(
                    _mm_loadu_si128(x16),
                    _mm_loadu_si128(y16))));
        kernel_fpu_end();

        bytes -= 16, ++x16, ++y16, ++z16;
    }
#endif // GF256_TARGET_MOBILE

    z1 = (uint8_t *)(z16);
    x1 = (uint8_t *)(x16);
    y1 = (uint8_t *)(y16);

    // Handle a block of 8 bytes
    eight = bytes & 8;
    if (eight) {
        uint64_t * GF256_RESTRICT z8 = (uint64_t *)(z1);
        const uint64_t * GF256_RESTRICT x8 = (const uint64_t *)(x1);
        const uint64_t * GF256_RESTRICT y8 = (const uint64_t *)(y1);
        *z8 ^= *x8 ^ *y8;
    }

    // Handle a block of 4 bytes
    four = bytes & 4;
    if (four) {
        uint32_t * GF256_RESTRICT z4 = (uint32_t *)(z1 + eight);
        const uint32_t * GF256_RESTRICT x4 = (const uint32_t *)(x1 + eight);
        const uint32_t * GF256_RESTRICT y4 = (const uint32_t *)(y1 + eight);
        *z4 ^= *x4 ^ *y4;
    }

    // Handle final bytes
    offset = eight + four;
    switch (bytes & 3) {
        case 3: z1[offset + 2] ^= x1[offset + 2] ^ y1[offset + 2];
        case 2: z1[offset + 1] ^= x1[offset + 1] ^ y1[offset + 1];
        case 1: z1[offset] ^= x1[offset] ^ y1[offset];
        default:
            break;
    }
}

extern void gf256_addset_mem(void * GF256_RESTRICT vz, const void * GF256_RESTRICT vx,
                                 const void * GF256_RESTRICT vy, int bytes)
{
    GF256_M128 * GF256_RESTRICT z16 = (GF256_M128*)(vz);
    const GF256_M128 * GF256_RESTRICT x16 = (const GF256_M128*)(vx);
    const GF256_M128 * GF256_RESTRICT y16 = (const GF256_M128*)(vy);

    uint8_t * GF256_RESTRICT z1;
    uint8_t * GF256_RESTRICT x1;
    uint8_t * GF256_RESTRICT y1;
    int eight, four, offset;

#if defined(GF256_TARGET_MOBILE)
# if defined(GF256_TRY_NEON)
    // Handle multiples of 64 bytes
    if (CpuHasNeon) {
        while (bytes >= 64) {
            GF256_M128 x0 = vld1q_u8((uint8_t*)x16);
            GF256_M128 x1 = vld1q_u8((uint8_t*)(x16 + 1));
            GF256_M128 x2 = vld1q_u8((uint8_t*)(x16 + 2));
            GF256_M128 x3 = vld1q_u8((uint8_t*)(x16 + 3));
            GF256_M128 y0 = vld1q_u8((uint8_t*)(y16));
            GF256_M128 y1 = vld1q_u8((uint8_t*)(y16 + 1));
            GF256_M128 y2 = vld1q_u8((uint8_t*)(y16 + 2));
            GF256_M128 y3 = vld1q_u8((uint8_t*)(y16 + 3));

            vst1q_u8((uint8_t*)z16,     veorq_u8(x0, y0));
            vst1q_u8((uint8_t*)(z16 + 1), veorq_u8(x1, y1));
            vst1q_u8((uint8_t*)(z16 + 2), veorq_u8(x2, y2));
            vst1q_u8((uint8_t*)(z16 + 3), veorq_u8(x3, y3));

            bytes -= 64, x16 += 4, y16 += 4, z16 += 4;
        }

        // Handle multiples of 16 bytes
        while (bytes >= 16) {
            // z[i] = x[i] xor y[i]
            vst1q_u8((uint8_t*)z16,
                     veorq_u8(
                         vld1q_u8((uint8_t*)x16),
                         vld1q_u8((uint8_t*)y16)));

            bytes -= 16, ++x16, ++y16, ++z16;
        }
    }
    else
# endif // GF256_TRY_NEON
    {
        unsigned ii;
        uint64_t * GF256_RESTRICT z8 = (uint64_t *)(z16);
        const uint64_t * GF256_RESTRICT x8 = (const uint64_t *)(x16);
        const uint64_t * GF256_RESTRICT y8 = (const uint64_t *)(y16);

        const unsigned count = (unsigned)bytes / 8;
        for (ii = 0; ii < count; ++ii) {
            z8[ii] = x8[ii] ^ y8[ii];
        }

        x16 = (const GF256_M128 *)(x8 + count);
        y16 = (const GF256_M128 *)(y8 + count);
        z16 = (GF256_M128 *)(z8 + count);

        bytes -= (count * 8);
    }
#else // GF256_TARGET_MOBILE
# if defined(GF256_TRY_AVX2)
    if (CpuHasAVX2) {
        GF256_M256 * GF256_RESTRICT z32 = (GF256_M256 *)(z16);
        const GF256_M256 * GF256_RESTRICT x32 = (const GF256_M256 *)(x16);
        const GF256_M256 * GF256_RESTRICT y32 = (const GF256_M256 *)(y16);
        unsigned i;
        const unsigned count = bytes / 32;
        for (i = 0; i < count; ++i) {
            kernel_fpu_begin();
            _mm256_storeu_si256(z32 + i,
                _mm256_xor_si256(
                    _mm256_loadu_si256(x32 + i),
                    _mm256_loadu_si256(y32 + i)));
            kernel_fpu_end();
        }

        bytes -= count * 32;
        z16 = (GF256_M128 *)(z32 + count);
        x16 = (const GF256_M128 *)(x32 + count);
        y16 = (const GF256_M128 *)(y32 + count);
    }
    else
# endif // GF256_TRY_AVX2
    {
        // Handle multiples of 64 bytes
        while (bytes >= 64) {
            GF256_M128 x0, x1, x2, x3, y0, y1, y2, y3;
            kernel_fpu_begin();
            x0 = _mm_loadu_si128(x16);
            x1 = _mm_loadu_si128(x16 + 1);
            x2 = _mm_loadu_si128(x16 + 2);
            x3 = _mm_loadu_si128(x16 + 3);
            y0 = _mm_loadu_si128(y16);
            y1 = _mm_loadu_si128(y16 + 1);
            y2 = _mm_loadu_si128(y16 + 2);
            y3 = _mm_loadu_si128(y16 + 3);

            _mm_storeu_si128(z16,     _mm_xor_si128(x0, y0));
            _mm_storeu_si128(z16 + 1, _mm_xor_si128(x1, y1));
            _mm_storeu_si128(z16 + 2, _mm_xor_si128(x2, y2));
            _mm_storeu_si128(z16 + 3, _mm_xor_si128(x3, y3));

            kernel_fpu_end();
            bytes -= 64, x16 += 4, y16 += 4, z16 += 4;
        }
    }

    // Handle multiples of 16 bytes
    while (bytes >= 16) {
        // z[i] = x[i] xor y[i]
        kernel_fpu_begin();
        _mm_storeu_si128(z16,
            _mm_xor_si128(
                _mm_loadu_si128(x16),
                _mm_loadu_si128(y16)));

        kernel_fpu_end();
        bytes -= 16, ++x16, ++y16, ++z16;
    }
#endif // GF256_TARGET_MOBILE

    z1 = (uint8_t *)(z16);
    x1 = (uint8_t *)(x16);
    y1 = (uint8_t *)(y16);

    // Handle a block of 8 bytes
    eight = bytes & 8;
    if (eight) {
        uint64_t * GF256_RESTRICT z8 = (uint64_t *)(z1);
        const uint64_t * GF256_RESTRICT x8 = (const uint64_t *)(x1);
        const uint64_t * GF256_RESTRICT y8 = (const uint64_t *)(y1);
        *z8 = *x8 ^ *y8;
    }

    // Handle a block of 4 bytes
    four = bytes & 4;
    if (four) {
        uint32_t * GF256_RESTRICT z4 = (uint32_t *)(z1 + eight);
        const uint32_t * GF256_RESTRICT x4 = (const uint32_t *)(x1 + eight);
        const uint32_t * GF256_RESTRICT y4 = (const uint32_t *)(y1 + eight);
        *z4 = *x4 ^ *y4;
    }

    // Handle final bytes
    offset = eight + four;
    switch (bytes & 3) {
        case 3: z1[offset + 2] = x1[offset + 2] ^ y1[offset + 2];
        case 2: z1[offset + 1] = x1[offset + 1] ^ y1[offset + 1];
        case 1: z1[offset] = x1[offset] ^ y1[offset];
        default:
            break;
    }
}

extern void gf256_mul_mem(void * GF256_RESTRICT vz, const void * GF256_RESTRICT vx, uint8_t y, int bytes)
{
    GF256_M128 * GF256_RESTRICT z16 = (GF256_M128 *)(vz);
    const GF256_M128 * GF256_RESTRICT x16 = (const GF256_M128 *)(vx);

    uint8_t * GF256_RESTRICT z1;
    uint8_t * GF256_RESTRICT x1;
    uint8_t * GF256_RESTRICT table;
    int offset, four;
    // Use a single if-statement to handle special cases
    if (y <= 1) {
        if (y == 0) {
            memset(vz, 0, bytes);
        } else if (vz != vx) {
            memcpy(vz, vx, bytes);
        }
        return;
    }


#if defined(GF256_TARGET_MOBILE)
#if defined(GF256_TRY_NEON)
    if (bytes >= 16 && CpuHasNeon) {
        // Partial product tables; see above
        const GF256_M128 table_lo_y = vld1q_u8((uint8_t*)(GF256Ctx.MM128.TABLE_LO_Y + y));
        const GF256_M128 table_hi_y = vld1q_u8((uint8_t*)(GF256Ctx.MM128.TABLE_HI_Y + y));

        // clr_mask = 0x0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f
        const GF256_M128 clr_mask = vdupq_n_u8(0x0f);

        // Handle multiples of 16 bytes
        do {
            // See above comments for details
            GF256_M128 x0 = vld1q_u8((uint8_t*)x16);
            GF256_M128 l0 = vandq_u8(x0, clr_mask);
            x0 = vshrq_n_u8(x0, 4);
            GF256_M128 h0 = vandq_u8(x0, clr_mask);
            l0 = vqtbl1q_u8(table_lo_y, l0);
            h0 = vqtbl1q_u8(table_hi_y, h0);
            vst1q_u8((uint8_t*)z16, veorq_u8(l0, h0));

            bytes -= 16, ++x16, ++z16;
        } while (bytes >= 16);
    }
#endif
#else
# if defined(GF256_TRY_AVX2)
    if (bytes >= 32 && CpuHasAVX2) {
        GF256_M256 table_lo_y, table_hi_y, clr_mask;
        // Partial product tables; see above
        kernel_fpu_begin();
        table_lo_y = _mm256_loadu_si256(GF256Ctx.MM256.TABLE_LO_Y + y);
        table_hi_y = _mm256_loadu_si256(GF256Ctx.MM256.TABLE_HI_Y + y);
        // clr_mask = 0x0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f
        clr_mask = _mm256_set1_epi8(0x0f);
        kernel_fpu_end();

        GF256_M256 * GF256_RESTRICT z32 = (GF256_M256 *)(vz);
        const GF256_M256 * GF256_RESTRICT x32 = (const GF256_M256 *)(vx);

        // Handle multiples of 32 bytes
        do {
            GF256_M256 x0, l0, h0;
            kernel_fpu_begin();
            // See above comments for details
            x0 = _mm256_loadu_si256(x32);
            l0 = _mm256_and_si256(x0, clr_mask);
            x0 = _mm256_srli_epi64(x0, 4);
            h0 = _mm256_and_si256(x0, clr_mask);
            l0 = _mm256_shuffle_epi8(table_lo_y, l0);
            h0 = _mm256_shuffle_epi8(table_hi_y, h0);
            _mm256_storeu_si256(z32, _mm256_xor_si256(l0, h0));

            kernel_fpu_end();
            bytes -= 32, ++x32, ++z32;
        } while (bytes >= 32);

        z16 = (GF256_M128 *)(z32);
        x16 = (const GF256_M128 *)(x32);
    }
# endif // GF256_TRY_AVX2
    if (bytes >= 16 && CpuHasSSSE3) {
        GF256_M128 table_lo_y, table_hi_y, clr_mask;
        // Partial product tables; see above
        kernel_fpu_begin();
        table_lo_y = _mm_loadu_si128(GF256Ctx.MM128.TABLE_LO_Y + y);
        table_hi_y = _mm_loadu_si128(GF256Ctx.MM128.TABLE_HI_Y + y);

        // clr_mask = 0x0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f
        clr_mask = _mm_set1_epi8(0x0f);
        kernel_fpu_end();

        // Handle multiples of 16 bytes
        do {
            GF256_M128 x0, l0, h0;
            kernel_fpu_begin();
            // See above comments for details
            x0 = _mm_loadu_si128(x16);
            l0 = _mm_and_si128(x0, clr_mask);
            x0 = _mm_srli_epi64(x0, 4);
            h0 = _mm_and_si128(x0, clr_mask);
            l0 = _mm_shuffle_epi8(table_lo_y, l0);
            h0 = _mm_shuffle_epi8(table_hi_y, h0);
            _mm_storeu_si128(z16, _mm_xor_si128(l0, h0));

            kernel_fpu_end();
            bytes -= 16, ++x16, ++z16;
        } while (bytes >= 16);
    }
#endif

    z1 = (uint8_t*)(z16);
    x1 = (uint8_t*)(x16);
    table = GF256Ctx.GF256_MUL_TABLE + ((unsigned)y << 8);
    // Handle blocks of 8 bytes
    while (bytes >= 8) {
        uint64_t * GF256_RESTRICT z8 = (uint64_t *)(z1);
        uint64_t word = table[x1[0]];
        word |= (uint64_t)table[x1[1]] << 8;
        word |= (uint64_t)table[x1[2]] << 16;
        word |= (uint64_t)table[x1[3]] << 24;
        word |= (uint64_t)table[x1[4]] << 32;
        word |= (uint64_t)table[x1[5]] << 40;
        word |= (uint64_t)table[x1[6]] << 48;
        word |= (uint64_t)table[x1[7]] << 56;
        *z8 = word;

        bytes -= 8, x1 += 8, z1 += 8;
    }

    // Handle a block of 4 bytes
    four = bytes & 4;
    if (four) {
        uint32_t * GF256_RESTRICT z4 = (uint32_t *)(z1);
        uint32_t word = table[x1[0]];
        word |= (uint32_t)table[x1[1]] << 8;
        word |= (uint32_t)table[x1[2]] << 16;
        word |= (uint32_t)table[x1[3]] << 24;
        *z4 = word;
    }

    // Handle single bytes
    offset = four;
    switch (bytes & 3) {
        case 3: z1[offset + 2] = table[x1[offset + 2]];
        case 2: z1[offset + 1] = table[x1[offset + 1]];
        case 1: z1[offset] = table[x1[offset]];
        default:
            break;
    }
}

extern void gf256_muladd_mem(void * GF256_RESTRICT vz, uint8_t y,
                                 const void * GF256_RESTRICT vx, int bytes)
{
    GF256_M128 * GF256_RESTRICT z16 = (GF256_M128 *)(vz);
    const GF256_M128 * GF256_RESTRICT x16 = (const GF256_M128 *)(vx);

    uint8_t * GF256_RESTRICT z1;
    uint8_t * GF256_RESTRICT x1;
    uint8_t * GF256_RESTRICT table;
    int four, offset;
    // Use a single if-statement to handle special cases
    if (y <= 1) {
        if (y == 1) {
            gf256_add_mem(vz, vx, bytes);
        }
        return;
    }

#if defined(GF256_TARGET_MOBILE)
#if defined(GF256_TRY_NEON)
    if (bytes >= 16 && CpuHasNeon) {
        // Partial product tables; see above
        const GF256_M128 table_lo_y = vld1q_u8((uint8_t*)(GF256Ctx.MM128.TABLE_LO_Y + y));
        const GF256_M128 table_hi_y = vld1q_u8((uint8_t*)(GF256Ctx.MM128.TABLE_HI_Y + y));

        // clr_mask = 0x0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f
        const GF256_M128 clr_mask = vdupq_n_u8(0x0f);

        // Handle multiples of 16 bytes
        do {
            // See above comments for details
            GF256_M128 x0 = vld1q_u8((uint8_t*)x16);
            GF256_M128 l0 = vandq_u8(x0, clr_mask);
            GF256_M128 h0, p0, z0;
            // x0 = vshrq_n_u8(x0, 4);
            x0 = (GF256_M128)vshrq_n_u64( (uint64x2_t)x0, 4);
            h0 = vandq_u8(x0, clr_mask);
            l0 = vqtbl1q_u8(table_lo_y, l0);
            h0 = vqtbl1q_u8(table_hi_y, h0);
            p0 = veorq_u8(l0, h0);
            z0 = vld1q_u8((uint8_t*)z16);
            vst1q_u8((uint8_t*)z16, veorq_u8(p0, z0));
            bytes -= 16, ++x16, ++z16;
        } while (bytes >= 16);
    }
#endif
#else // GF256_TARGET_MOBILE
# if defined(GF256_TRY_AVX2)
    if (bytes >= 32 && CpuHasAVX2) {
        // Partial product tables; see above
        GF256_M256 table_lo_y, table_hi_y, clr_mask;
        kernel_fpu_begin();
        table_lo_y = _mm256_loadu_si256(GF256Ctx.MM256.TABLE_LO_Y + y);
        table_hi_y = _mm256_loadu_si256(GF256Ctx.MM256.TABLE_HI_Y + y);

        // clr_mask = 0x0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f
        clr_mask = _mm256_set1_epi8(0x0f);
        kernel_fpu_end();

        GF256_M256 * GF256_RESTRICT z32 = (GF256_M256 *)(z16);
        const GF256_M256 * GF256_RESTRICT x32 = (const GF256_M256 *)(x16);

        // On my Reed Solomon codec, the encoder unit test runs in 640 usec without and 550 usec with the optimization (86% of the original time)
        const unsigned count = bytes / 64;
        unsigned i;
        for (i = 0; i < count; ++i) {
            GF256_M256 x0, l0, z0, p0, x1, l1, z1, p1;
            kernel_fpu_begin();
            // See above comments for details
            x0 = _mm256_loadu_si256(x32 + i * 2);
            l0 = _mm256_and_si256(x0, clr_mask);
            x0 = _mm256_srli_epi64(x0, 4);
            z0 = _mm256_loadu_si256(z32 + i * 2);
            h0 = _mm256_and_si256(x0, clr_mask);
            l0 = _mm256_shuffle_epi8(table_lo_y, l0);
            h0 = _mm256_shuffle_epi8(table_hi_y, h0);
            p0 = _mm256_xor_si256(l0, h0);
            _mm256_storeu_si256(z32 + i * 2, _mm256_xor_si256(p0, z0));

            x1 = _mm256_loadu_si256(x32 + i * 2 + 1);
            l1 = _mm256_and_si256(x1, clr_mask);
            x1 = _mm256_srli_epi64(x1, 4);
            z1 = _mm256_loadu_si256(z32 + i * 2 + 1);
            h1 = _mm256_and_si256(x1, clr_mask);
            l1 = _mm256_shuffle_epi8(table_lo_y, l1);
            h1 = _mm256_shuffle_epi8(table_hi_y, h1);
            p1 = _mm256_xor_si256(l1, h1);
            _mm256_storeu_si256(z32 + i * 2 + 1, _mm256_xor_si256(p1, z1));
            kernel_fpu_end();
        }
        bytes -= count * 64;
        z32 += count * 2;
        x32 += count * 2;

        if (bytes >= 32) {
            GF256_M256 x0, l0, h0, p0, z0;
            kernel_fpu_begin();
            x0 = _mm256_loadu_si256(x32);
            l0 = _mm256_and_si256(x0, clr_mask);
            x0 = _mm256_srli_epi64(x0, 4);
            h0 = _mm256_and_si256(x0, clr_mask);
            l0 = _mm256_shuffle_epi8(table_lo_y, l0);
            h0 = _mm256_shuffle_epi8(table_hi_y, h0);
            p0 = _mm256_xor_si256(l0, h0);
            z0 = _mm256_loadu_si256(z32);
            _mm256_storeu_si256(z32, _mm256_xor_si256(p0, z0));

            kernel_fpu_end();
            bytes -= 32;
            z32++;
            x32++;
        }

        z16 = (GF256_M128 *)(z32);
        x16 = (const GF256_M128 *)(x32);
    }
# endif // GF256_TRY_AVX2
    if (bytes >= 16 && CpuHasSSSE3) {
        // Partial product tables; see above
        GF256_M128 table_lo_y, table_hi_y, clr_mask;
        kernel_fpu_begin();
        table_lo_y = _mm_loadu_si128(GF256Ctx.MM128.TABLE_LO_Y + y);
        table_hi_y = _mm_loadu_si128(GF256Ctx.MM128.TABLE_HI_Y + y);

        // clr_mask = 0x0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f
        clr_mask = _mm_set1_epi8(0x0f);
        kernel_fpu_end();

        // This unroll seems to provide about 7% speed boost when AVX2 is disabled
        while (bytes >= 32) {
            GF256_M128 x1, l1, h1, z1, h0, z0, x0, l0, p0, p1;
            kernel_fpu_begin();

            x1 = _mm_loadu_si128(x16 + 1);
            l1 = _mm_and_si128(x1, clr_mask);

            bytes -= 32;
            x1 = _mm_srli_epi64(x1, 4);
            h1 = _mm_and_si128(x1, clr_mask);
            l1 = _mm_shuffle_epi8(table_lo_y, l1);
            h1 = _mm_shuffle_epi8(table_hi_y, h1);
            z1 = _mm_loadu_si128(z16 + 1);

            x0 = _mm_loadu_si128(x16);
            l0 = _mm_and_si128(x0, clr_mask);
            x0 = _mm_srli_epi64(x0, 4);
            h0 = _mm_and_si128(x0, clr_mask);
            l0 = _mm_shuffle_epi8(table_lo_y, l0);
            h0 = _mm_shuffle_epi8(table_hi_y, h0);
            z0 = _mm_loadu_si128(z16);

            p1 = _mm_xor_si128(l1, h1);
            _mm_storeu_si128(z16 + 1, _mm_xor_si128(p1, z1));

            p0 = _mm_xor_si128(l0, h0);
            _mm_storeu_si128(z16, _mm_xor_si128(p0, z0));

            kernel_fpu_end();
            x16 += 2, z16 += 2;
        }

        // Handle multiples of 16 bytes
        while (bytes >= 16) {
            // See above comments for details
            GF256_M128 x0, h0, l0, p0, z0;
            kernel_fpu_begin();
            x0 = _mm_loadu_si128(x16);
            l0 = _mm_and_si128(x0, clr_mask);
            x0 = _mm_srli_epi64(x0, 4);
            h0 = _mm_and_si128(x0, clr_mask);
            l0 = _mm_shuffle_epi8(table_lo_y, l0);
            h0 = _mm_shuffle_epi8(table_hi_y, h0);
            p0 = _mm_xor_si128(l0, h0);
            z0 = _mm_loadu_si128(z16);
            _mm_storeu_si128(z16, _mm_xor_si128(p0, z0));
            kernel_fpu_end();
            bytes -= 16, ++x16, ++z16;
        }
    }
#endif // GF256_TARGET_MOBILE

    z1 = (uint8_t*)(z16);
    x1 = (uint8_t*)(x16);
    table = GF256Ctx.GF256_MUL_TABLE + ((unsigned)y << 8);
    

    // Handle blocks of 8 bytes
    while (bytes >= 8) {
        uint64_t * GF256_RESTRICT z8 = (uint64_t *)(z1);
        uint64_t word = table[x1[0]];
        word |= (uint64_t)table[x1[1]] << 8;
        word |= (uint64_t)table[x1[2]] << 16;
        word |= (uint64_t)table[x1[3]] << 24;
        word |= (uint64_t)table[x1[4]] << 32;
        word |= (uint64_t)table[x1[5]] << 40;
        word |= (uint64_t)table[x1[6]] << 48;
        word |= (uint64_t)table[x1[7]] << 56;
        *z8 ^= word;

        bytes -= 8, x1 += 8, z1 += 8;
    }

    // Handle a block of 4 bytes
    four = bytes & 4;
    if (four) {
        uint32_t * GF256_RESTRICT z4 = (uint32_t *)(z1);
        uint32_t word = table[x1[0]];
        word |= (uint32_t)table[x1[1]] << 8;
        word |= (uint32_t)table[x1[2]] << 16;
        word |= (uint32_t)table[x1[3]] << 24;
        *z4 ^= word;
    }

    // Handle single bytes
    offset = four;
    switch (bytes & 3) {
        case 3: z1[offset + 2] ^= table[x1[offset + 2]];
        case 2: z1[offset + 1] ^= table[x1[offset + 1]];
        case 1: z1[offset] ^= table[x1[offset]];
        default:
            break;
    }
}

extern void gf256_memswap(void * GF256_RESTRICT vx, void * GF256_RESTRICT vy, int bytes) {
    int eight, four, offset;
    uint8_t * GF256_RESTRICT x1;
    uint8_t * GF256_RESTRICT y1;
    uint8_t temp2;
#if defined(GF256_TARGET_MOBILE)
    uint64_t * GF256_RESTRICT x16 = (uint64_t *)(vx);
    uint64_t * GF256_RESTRICT y16 = (uint64_t *)(vy);
    unsigned ii;
    const unsigned count = (unsigned)bytes / 8;
    for (ii = 0; ii < count; ++ii) {
        const uint64_t temp = x16[ii];
        x16[ii] = y16[ii];
        y16[ii] = temp;
    }

    x16 += count;
    y16 += count;
#else
    GF256_M128 * GF256_RESTRICT x16 = (GF256_M128 *)(vx);
    GF256_M128 * GF256_RESTRICT y16 = (GF256_M128 *)(vy);

    // Handle blocks of 16 bytes
    while (bytes >= 16) {
        GF256_M128 x0, y0;
        kernel_fpu_begin();
        x0 = _mm_loadu_si128(x16);
        y0 = _mm_loadu_si128(y16);
        _mm_storeu_si128(x16, y0);
        _mm_storeu_si128(y16, x0);
        kernel_fpu_end();

        bytes -= 16, ++x16, ++y16;
    }
#endif

    x1 = (uint8_t *)(x16);
    y1 = (uint8_t *)(y16);


    // Handle a block of 8 bytes
    eight = bytes & 8;
    if (eight) {
        uint64_t * GF256_RESTRICT x8 = (uint64_t *)(x1);
        uint64_t * GF256_RESTRICT y8 = (uint64_t *)(y1);

        uint64_t temp = *x8;
        *x8 = *y8;
        *y8 = temp;
    }

    // Handle a block of 4 bytes
    four = bytes & 4;
    if (four) {
        uint32_t * GF256_RESTRICT x4 = (uint32_t *)(x1 + eight);
        uint32_t * GF256_RESTRICT y4 = (uint32_t *)(y1 + eight);

        uint32_t temp = *x4;
        *x4 = *y4;
        *y4 = temp;
    }

    // Handle final bytes
    offset = eight + four;
    switch (bytes & 3) {
        case 3: temp2 = x1[offset + 2]; x1[offset + 2] = y1[offset + 2]; y1[offset + 2] = temp2;
        case 2: temp2 = x1[offset + 1]; x1[offset + 1] = y1[offset + 1]; y1[offset + 1] = temp2;
        case 1: temp2 = x1[offset]; x1[offset] = y1[offset]; y1[offset] = temp2;
        default:
            break;
    }
}

/*
    GF(256) Cauchy Matrix Overview

    As described on Wikipedia, each element of a normal Cauchy matrix is defined as:

        a_ij = 1 / (x_i - y_j)
        The arrays x_i and y_j are vector parameters of the matrix.
        The values in x_i cannot be reused in y_j.

    Moving beyond the Wikipedia...

    (1) Number of rows (R) is the range of i, and number of columns (C) is the range of j.

    (2) Being able to select x_i and y_j makes Cauchy matrices more flexible in practice
        than Vandermonde matrices, which only have one parameter per row.

    (3) Cauchy matrices are always invertible, AKA always full rank, AKA when treated as
        as linear system y = M*x, the linear system has a single solution.

    (4) A Cauchy matrix concatenated below a square CxC identity matrix always has rank C,
        Meaning that any R rows can be eliminated from the concatenated matrix and the
        matrix will still be invertible.  This is how Reed-Solomon erasure codes work.

    (5) Any row or column can be multiplied by non-zero values, and the resulting matrix
        is still full rank.  This is true for any matrix, since it is effectively the same
        as pre and post multiplying by diagonal matrices, which are always invertible.

    (6) Matrix elements with a value of 1 are much faster to operate on than other values.
        For instance a matrix of [1, 1, 1, 1, 1] is invertible and much faster for various
        purposes than [2, 2, 2, 2, 2].

    (7) For GF(256) matrices, the symbols in x_i and y_j are selected from the numbers
        0...255, and so the number of rows + number of columns may not exceed 256.
        Note that values in x_i and y_j may not be reused as stated above.

    In summary, Cauchy matrices
        are preferred over Vandermonde matrices.  (2)
        are great for MDS erasure codes.  (3) and (4)
        should be optimized to include more 1 elements.  (5) and (6)
        have a limited size in GF(256), rows+cols <= 256.  (7)
*/


//-----------------------------------------------------------------------------
// Initialization

extern int cm256_init_(int version){
    if (version != CM256_VERSION){
        // User's header does not match library version
        return -10;
    }

    // Return error code from GF(256) init if required
    return gf256_init();
}


/*
    Selected Cauchy Matrix Form

    The matrix consists of elements a_ij, where i = row, j = column.
    a_ij = 1 / (x_i - y_j), where x_i and y_j are sets of GF(256) values
    that do not intersect.

    We select x_i and y_j to just be incrementing numbers for the
    purposes of this library.  Further optimizations may yield matrices
    with more 1 elements, but the benefit seems relatively small.

    The x_i values range from 0...(originalCount - 1).
    The y_j values range from originalCount...(originalCount + recoveryCount - 1).

    We then improve the Cauchy matrix by dividing each column by the
    first row element of that column.  The result is an invertible
    matrix that has all 1 elements in the first row.  This is equivalent
    to a rotated Vandermonde matrix, so we could have used one of those.

    The advantage of doing this is that operations involving the first
    row will be extremely fast (just memory XOR), so the decoder can
    be optimized to take advantage of the shortcut when the first
    recovery row can be used.

    First row element of Cauchy matrix for each column:
    a_0j = 1 / (x_0 - y_j) = 1 / (x_0 - y_j)

    Our Cauchy matrix sets first row to ones, so:
    a_ij = (1 / (x_i - y_j)) / a_0j
    a_ij = (y_j - x_0) / (x_i - y_j)
    a_ij = (y_j + x_0) div (x_i + y_j) in GF(256)
*/

// This function generates each matrix element based on x_i, x_0, y_j
// Note that for x_i == x_0, this will return 1, so it is better to unroll out the first row.
static GF256_FORCE_INLINE unsigned char GetMatrixElement(unsigned char x_i, unsigned char x_0, unsigned char y_j){
    return gf256_div(gf256_add(y_j, x_0), gf256_add(x_i, y_j));
}


//-----------------------------------------------------------------------------
// Encoding

extern void cm256_encode_block(
    cm256_encoder_params params, // Encoder parameters
    cm256_block* originals,      // Array of pointers to original blocks
    int recoveryBlockIndex,      // Return value from cm256_get_recovery_block_index()
    void* recoveryBlock)         // Output recovery block
{   
    uint8_t x_0, x_i, y_0, y_j, matrixElement;
    int j;
    // If only one block of input data,
    if (params.OriginalCount == 1){
        // No meaningful operation here, degenerate to outputting the same data each time.

        memcpy(recoveryBlock, originals[0].Block, params.BlockBytes);
        return;
    }
    // else OriginalCount >= 2:

    // Unroll first row of recovery matrix:
    // The matrix we generate for the first row is all ones,
    // so it is merely a parity of the original data.
    if (recoveryBlockIndex == params.OriginalCount){
        gf256_addset_mem(recoveryBlock, originals[0].Block, originals[1].Block, params.BlockBytes);
        for (j = 2; j < params.OriginalCount; ++j){
            gf256_add_mem(recoveryBlock, originals[j].Block, params.BlockBytes);
        }
        return;
    }

    // TBD: Faster algorithms seem to exist for computing this matrix-vector product.

    // Start the x_0 values arbitrarily from the original count.
    x_0 = (uint8_t)(params.OriginalCount);

    // For other rows:
    {
        x_i = (uint8_t)(recoveryBlockIndex);

        // Unroll first operation for speed
        {
            y_0 = 0;
            matrixElement = GetMatrixElement(x_i, x_0, y_0);

            gf256_mul_mem(recoveryBlock, originals[0].Block, matrixElement, params.BlockBytes);
        }

        // For each original data column,
        for (j = 1; j < params.OriginalCount; ++j){
            y_j = (uint8_t)(j);
            matrixElement = GetMatrixElement(x_i, x_0, y_j);

            gf256_muladd_mem(recoveryBlock, matrixElement, originals[j].Block, params.BlockBytes);
        }
    }
}

extern int cm256_encode(
    cm256_encoder_params params, // Encoder params
    cm256_block* originals,      // Array of pointers to original blocks
    void* recoveryBlocks)        // Output recovery blocks end-to-end
{
    uint8_t* recoveryBlock;
    int block;

    // Validate input:
    if (params.OriginalCount <= 0 || params.RecoveryCount <= 0 || params.BlockBytes <= 0){
        return -1;
    }
    if (params.OriginalCount + params.RecoveryCount > 256){
        return -2;
    }
    if (!originals || !recoveryBlocks){
        return -3;
    }

    recoveryBlock = (uint8_t*)(recoveryBlocks);

    for (block = 0; block < params.RecoveryCount; ++block, recoveryBlock += params.BlockBytes){
        cm256_encode_block(params, originals, (params.OriginalCount + block), recoveryBlock);
    }

    return 0;
}


//-----------------------------------------------------------------------------
// Decoding

typedef struct {
    // Encode parameters
    cm256_encoder_params Params;

    // Recovery blocks
    cm256_block* Recovery[256];
    int RecoveryCount;

    // Original blocks
    cm256_block* Original[256];
    int OriginalCount;

    // Row indices that were erased
    uint8_t ErasuresIndices[256];

    // Initialize the decoder

    // Decode m=1 case

    // Decode for m>1 case

    // Generate the LU decomposition of the matrix
}CM256Decoder;

int Initialize(CM256Decoder *decoder, cm256_encoder_params params, cm256_block* blocks) {
    int ii, row, indexCount;
    cm256_block* block = blocks;
    decoder->Params = params;

    decoder->OriginalCount = 0;
    decoder->RecoveryCount = 0;

    // Initialize erasures to zeros
    for (ii = 0; ii < params.OriginalCount; ++ii){
        decoder->ErasuresIndices[ii] = 0;
    }

    // For each input block,
    for (ii = 0; ii < params.OriginalCount; ++ii, ++block){
        row = block->Index;

        // If it is an original block,
        if (row < params.OriginalCount){
            decoder->Original[decoder->OriginalCount++] = block;
            if (decoder->ErasuresIndices[row] != 0){
                // Error out if two row indices repeat
                printk(KERN_INFO "Indices incorrect\n");
                return -1;
            }
            decoder->ErasuresIndices[row] = 1;
        }
        else{
            decoder->Recovery[decoder->RecoveryCount++] = block;
        }
    }

    // Identify erasures
    for (ii = 0, indexCount = 0; ii < 256; ++ii) {
        if (!decoder->ErasuresIndices[ii]) {
            decoder->ErasuresIndices[indexCount] = (uint8_t)( ii );

            if (++indexCount >= decoder->RecoveryCount) {
                break;
            }
        }
    }
    return 0;
}

void DecodeM1(CM256Decoder *decoder){
    // XOR all other blocks into the recovery block
    uint8_t* outBlock = (uint8_t*)(decoder->Recovery[0]->Block);
    uint8_t* inBlock = NULL;
    int ii;
    uint8_t* inBlock2;

    // For each block,
    for (ii = 0; ii < decoder->OriginalCount; ++ii) {
        inBlock2 = (uint8_t*)(decoder->Original[ii]->Block);

        if (!inBlock) {
            inBlock = inBlock2;
        }else {
            // outBlock ^= inBlock ^ inBlock2
            gf256_add2_mem(outBlock, inBlock, inBlock2, decoder->Params.BlockBytes);
            inBlock = NULL;
        }
    }

    // Complete XORs
    if (inBlock) {
        gf256_add_mem(outBlock, inBlock, decoder->Params.BlockBytes);
    }

    // Recover the index it corresponds to
    decoder->Recovery[0]->Index = decoder->ErasuresIndices[0];
}

// Generate the LU decomposition of the matrix
void GenerateLDUDecomposition(CM256Decoder *decoder, uint8_t* matrix_L, uint8_t* diag_D, uint8_t* matrix_U) {
    // Schur-type-direct-Cauchy algorithm 2.5 from
    // "Pivoting and Backward Stability of Fast Algorithms for Solving Cauchy Linear Equations"
    // T. Boros, T. Kailath, V. Olshevsky
    // Modified for practical use.  I folded the diagonal parts of U/L matrices into the
    // diagonal one to reduce the number of multiplications to perform against the input data,
    // and organized the triangle matrices in memory to allow for faster SSE3 GF multiplications.

    // Matrix size NxN
    int N = decoder->RecoveryCount;
    int count;
    int i, firstOffset_U, j, k;
    uint8_t rotated_row_U[256];
    uint8_t *last_U, *row_L, *row_U, *output_U;
    uint8_t x_0, x_k, y_k, D_kk, L_kk, U_kk, x_j, y_j, L_jk, U_kj, x_n, y_n, L_nn, U_nn;

    // Generators
    uint8_t g[256], b[256];
    for (i = 0; i < N; ++i) {
        g[i] = 1;
        b[i] = 1;
    }

    // Temporary buffer for rotated row of U matrix
    // This allows for faster GF bulk multiplication
    last_U = matrix_U + ((N - 1) * N) / 2 - 1;
    firstOffset_U = 0;

    // Start the x_0 values arbitrarily from the original count.
    x_0 = (uint8_t)(decoder->Params.OriginalCount);

    // Unrolling k = 0 just makes it slower for some reason.
    for (k = 0; k < N - 1; ++k) {
        x_k = decoder->Recovery[k]->Index;
        y_k = decoder->ErasuresIndices[k];

        // D_kk = (x_k + y_k)
        // L_kk = g[k] / (x_k + y_k)
        // U_kk = b[k] * (x_0 + y_k) / (x_k + y_k)
        D_kk = gf256_add(x_k, y_k);
        L_kk = gf256_div(g[k], D_kk);
        U_kk = gf256_mul(gf256_div(b[k], D_kk), gf256_add(x_0, y_k));

        // diag_D[k] = D_kk * L_kk * U_kk
        diag_D[k] = gf256_mul(D_kk, gf256_mul(L_kk, U_kk));

        // Computing the k-th row of L and U
        row_L = matrix_L;
        row_U = rotated_row_U;
        for (j = k + 1; j < N; ++j) {
            x_j = decoder->Recovery[j]->Index;
            y_j = decoder->ErasuresIndices[j];

            // L_jk = g[j] / (x_j + y_k)
            // U_kj = b[j] / (x_k + y_j)
            L_jk = gf256_div(g[j], gf256_add(x_j, y_k));
            U_kj = gf256_div(b[j], gf256_add(x_k, y_j));

            *matrix_L++ = L_jk;
            *row_U++ = U_kj;

            // g[j] = g[j] * (x_j + x_k) / (x_j + y_k)
            // b[j] = b[j] * (y_j + y_k) / (y_j + x_k)
            g[j] = gf256_mul(g[j], gf256_div(gf256_add(x_j, x_k), gf256_add(x_j, y_k)));
            b[j] = gf256_mul(b[j], gf256_div(gf256_add(y_j, y_k), gf256_add(y_j, x_k)));
        }

        // Do these row/column divisions in bulk for speed.
        // L_jk /= L_kk
        // U_kj /= U_kk
        count = N - (k + 1);
        gf256_div_mem(row_L, row_L, L_kk, count);
        gf256_div_mem(rotated_row_U, rotated_row_U, U_kk, count);

        // Copy U matrix row into place in memory.
        output_U = last_U + firstOffset_U;
        row_U = rotated_row_U;
        for (j = k + 1; j < N; ++j) {
            *output_U = *row_U++;
            output_U -= j;
        }
        firstOffset_U -= k + 2;
    }

    // Multiply diagonal matrix into U
    row_U = matrix_U;
    for (j = N - 1; j > 0; --j) {
        y_j = decoder->ErasuresIndices[j];
        count = j;

        gf256_mul_mem(row_U, row_U, gf256_add(x_0, y_j), count);
        row_U += count;
    }

    x_n = decoder->Recovery[N - 1]->Index;
    y_n = decoder->ErasuresIndices[N - 1];

    // D_nn = 1 / (x_n + y_n)
    // L_nn = g[N-1]
    // U_nn = b[N-1] * (x_0 + y_n)
    L_nn = g[N - 1];
    U_nn = gf256_mul(b[N - 1], gf256_add(x_0, y_n));

    // diag_D[N-1] = L_nn * D_nn * U_nn
    diag_D[N - 1] = gf256_div(gf256_mul(L_nn, U_nn), gf256_add(x_n, y_n));
}

void Decode(CM256Decoder *decoder) {
    // Matrix size is NxN, where N is the number of recovery blocks used.
    const int N = decoder->RecoveryCount;

    // Start the x_0 values arbitrarily from the original count.
    const uint8_t x_0 = (uint8_t)(decoder->Params.OriginalCount);

    int originalIndex, recoveryIndex, j, i;
    int requiredSpace;
    uint8_t *inBlock, *outBlock, *dynamicMatrix, *matrix, *matrix_U, *diag_D, *matrix_L;
    uint8_t inRow, x_i, y_j, matrixElement, c_ij;
    static const int StackAllocSize = 2048;
    uint8_t stackMatrix[StackAllocSize];
    void *block_j;
    void *block_i, *block;
    // Eliminate original data from the the recovery rows
    for (originalIndex = 0; originalIndex < decoder->OriginalCount; ++originalIndex) {
        inBlock = (uint8_t*)(decoder->Original[originalIndex]->Block);
        inRow = decoder->Original[originalIndex]->Index;

        for (recoveryIndex = 0; recoveryIndex < N; ++recoveryIndex) {
            outBlock = (uint8_t*)(decoder->Recovery[recoveryIndex]->Block);
            x_i = decoder->Recovery[recoveryIndex]->Index;
            y_j = inRow;
            matrixElement = GetMatrixElement(x_i, x_0, y_j);

            gf256_muladd_mem(outBlock, matrixElement, inBlock, decoder->Params.BlockBytes);
        }
    }

    // Allocate matrix
    dynamicMatrix = NULL;
    matrix = stackMatrix;
    requiredSpace = N * N;
    if (requiredSpace > StackAllocSize) {
        dynamicMatrix = kmalloc(requiredSpace, GFP_KERNEL);
        matrix = dynamicMatrix;
    }

    /*
        Compute matrix decomposition:

            G = L * D * U

        L is lower-triangular, diagonal is all ones.
        D is a diagonal matrix.
        U is upper-triangular, diagonal is all ones.
    */
    matrix_U = matrix;
    diag_D = matrix_U + (N - 1) * N / 2;
    matrix_L = diag_D + N;
    GenerateLDUDecomposition(decoder, matrix_L, diag_D, matrix_U);

    /*
        Eliminate lower left triangle.
    */
    // For each column,
    for (j = 0; j < N - 1; ++j) {
        block_j = decoder->Recovery[j]->Block;

        // For each row,
        for (i = j + 1; i < N; ++i) {
            block_i = decoder->Recovery[i]->Block;
            c_ij = *matrix_L++; // Matrix elements are stored column-first, top-down.

            gf256_muladd_mem(block_i, c_ij, block_j, decoder->Params.BlockBytes);
        }
    }

    /*
        Eliminate diagonal.
    */
    for (i = 0; i < N; ++i) {
        block = decoder->Recovery[i]->Block;

        decoder->Recovery[i]->Index = decoder->ErasuresIndices[i];

        gf256_div_mem(block, block, diag_D[i], decoder->Params.BlockBytes);
    }

    /*
        Eliminate upper right triangle.
    */
    for (j = N - 1; j >= 1; --j) {
        block_j = decoder->Recovery[j]->Block;

        for (i = j - 1; i >= 0; --i) {
            block_i = decoder->Recovery[i]->Block;
            c_ij = *matrix_U++; // Matrix elements are stored column-first, bottom-up.

            gf256_muladd_mem(block_i, c_ij, block_j, decoder->Params.BlockBytes);
        }
    }

    kfree(dynamicMatrix);
}

extern int cm256_decode(
    cm256_encoder_params params, // Encoder params
    cm256_block* blocks)         // Array of 'originalCount' blocks as described above
{
    CM256Decoder *state = kmalloc(sizeof(CM256Decoder), GFP_KERNEL);

    if (params.OriginalCount <= 0 || params.RecoveryCount <= 0 || params.BlockBytes <= 0) {
        return -1;
    }
    if (params.OriginalCount + params.RecoveryCount > 256) {
        return -2;
    }
    if (!blocks) {
        return -3;
    }

    // If there is only one block,
    if (params.OriginalCount == 1) {
        // It is the same block repeated
        blocks[0].Index = 0;
        return 0;
    }

    if (Initialize(state, params, blocks)) {
        return -5;
    }

    // If nothing is erased,
    if (state->RecoveryCount <= 0) {
        return 0;
    }

    // If m=1,
    if (params.RecoveryCount == 1) {
        DecodeM1(state);
        return 0;
    }

    // Decode for m>1
    Decode(state);
    return 0;
}
