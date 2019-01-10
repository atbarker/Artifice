/*
 * Author: Yash Gupta <yash_gupta12@live.com>
 * Copyright: Yash Gupta
 * License: MIT Public License
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>

#ifndef _BIT_VECTOR_H_
#define _BIT_VECTOR_H_

// Standard integer types.
typedef s8 int8_t;
typedef u8 uint8_t;
typedef s16 int16_t;
typedef u16 uint16_t;
typedef s32 int32_t;
typedef u32 uint32_t;
typedef s64 int64_t;
typedef u64 uint64_t;

// Unit conversion.
#define BIT_VECTOR_BITS_IN_BYTE 8
#define BIT_VECTOR_BITS_TO_BYTES(b) ((b / BIT_VECTOR_BITS_IN_BYTE) + 1)
#define BIT_VECTOR_BYTES_TO_BITS(b) (b * BIT_VECTOR_BITS_TO_BYTES)

// Unit indexes for byte arrays.
//  Eg: For i==17, byte = 2 and bit = 1
#define BIT_VECTOR_GET_BYTE_INDEX(i) (i / BIT_VECTOR_BITS_IN_BYTE)
#define BIT_VECTOR_GET_BIT_INDEX(i) (i & 0x7)

// Main structure for data structure.
typedef struct _bit_vector_t {
    uint8_t *array;
    uint64_t length;
} bit_vector_t;

/**
 * Create and initialize a bit vector with its default values
 * depending on the type.
 */
bit_vector_t *
bit_vector_create(uint64_t length);

/**
 * Clear all the memory being utilized by the bit vector.
 */
void bit_vector_free(bit_vector_t *vector);

/**
 * Set a specific bit in the bit vector. Like any array, the index
 * begins from 0.
 */
int bit_vector_set(bit_vector_t *vector, uint64_t index);

/**
 * Clear a specific bit in the bit vector. Like any array, the index
 * begins from 0.
 */
int bit_vector_clear(bit_vector_t *vector, uint64_t index);

/**
 * Acquire the state of a bit in the bit vector. Like any array, the index
 * begins from 0.
 */
int bit_vector_get(bit_vector_t *vector, uint64_t index);

#endif /* _BIT_VECTOR_H_ */