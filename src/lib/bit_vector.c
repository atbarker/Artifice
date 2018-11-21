/*
 * Author: Yash Gupta <yash_gupta12@live.com>
 * Copyright: Yash Gupta
 * License: MIT Public License
 */
#include <lib/bit_vector.h>

/**
 * Create and initialize a bit vector with its default values
 * depending on the type.
 * 
 * @param   length      The length of the bit vector we need.
 *
 * @return  A bit vector
 * @return  NULL        Not enough memory.
 */
bit_vector_t*
bit_vector_create(uint64_t length)
{
    bit_vector_t* vector;
    uint64_t temp_length;

    temp_length = length;
    vector = kmalloc(sizeof *vector, GFP_KERNEL);
    if (!vector) {
        return NULL;
    }

    vector->array = vmalloc(BIT_VECTOR_BITS_TO_BYTES(temp_length) * sizeof *(vector->array));
    if (!(vector->array)) {
        kfree(vector);
        return NULL;
    }
    memset(vector->array, 0, BIT_VECTOR_BITS_TO_BYTES(temp_length) * sizeof *(vector->array));
    vector->length = temp_length;

    return vector;
}

/**
 * Clear all the memory being utilized by the bit vector.
 *
 * @param   vector      The bit vector to free
 */
void bit_vector_free(bit_vector_t* vector)
{
    vfree(vector->array);
    kfree(vector);
}

/**
 * Set a specific bit in the bit vector. Like any array, the index
 * begins from 0.
 *
 * @param   vector      The vector to set bit for.
 * @param   index       The index of the bit to set.
 *
 * @return   0          No error.
 *  EINVAL: vector is NULL.
 *  EINVAL: index is beyond vector length.
 */
int bit_vector_set(bit_vector_t* vector, uint64_t index)
{
    uint8_t or_bits;

    if (!vector || index >= vector->length) {
        return -EINVAL;
    }

    or_bits = 1 << BIT_VECTOR_GET_BIT_INDEX(index);
    vector->array[BIT_VECTOR_GET_BYTE_INDEX(index)] |= or_bits;

    return 0;
}

/**
 * Clear a specific bit in the bit vector. Like any array, the index
 * begins from 0.
 *
 * @param   vector      The vector to clear bit for.
 * @param   index       The index of the bit to clear.
 *
 * @return   0          No error.
 *  EINVAL: vector is NULL.
 *  EINVAL: index is beyond vector length.
 */
int bit_vector_clear(bit_vector_t* vector, uint64_t index)
{
    uint8_t and_bits;

    if (!vector || index >= vector->length) {
        return -EINVAL;
    }

    and_bits = ~(1 << BIT_VECTOR_GET_BIT_INDEX(index));
    vector->array[BIT_VECTOR_GET_BYTE_INDEX(index)] &= and_bits;

    return 0;
}

/**
 * Acquire the state of a bit in the bit vector. Like any array, the index
 * begins from 0.
 *
 * @param   vector      The vector to acquire bit for.
 * @param   index       The index of the bit to acquire.
 *
 * @return   1          Bit is set.
 * @return   0          Bit is clear.
 *  EINVAL: vector is NULL.
 *  EINVAL: index is beyond vector length.
 */
int bit_vector_get(bit_vector_t* vector, uint64_t index)
{
    int8_t return_bits;
    int8_t and_bits;

    if (!vector || index >= vector->length) {
        return -EINVAL;
    }

    and_bits = 1 << BIT_VECTOR_GET_BIT_INDEX(index);
    return_bits = vector->array[BIT_VECTOR_GET_BYTE_INDEX(index)] & and_bits;
    return !!return_bits;
}