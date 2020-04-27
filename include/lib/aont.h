#ifndef AONT_H
#define AONT_H

#include <linux/random.h>
#include <linux/types.h>
#include "cauchy_rs.h"
#include <linux/crypto.h>
#include <linux/scatterlist.h>
#include <crypto/skcipher.h>
#include <crypto/hash.h>

#define CANARY_SIZE 16
#define KEY_SIZE 32

static inline size_t get_share_size(size_t data_length, size_t data_blocks){
    return (data_length + CANARY_SIZE + KEY_SIZE) / data_blocks;
}

int encrypt_payload(uint8_t *data, const size_t datasize, uint8_t *key, size_t keylength, int enc);

int encode_aont_package(uint8_t *canary, uint8_t *difference, const uint8_t *data, size_t data_length, uint8_t **shares, size_t data_blocks, size_t parity_blocks);

int decode_aont_package(uint8_t *canary, uint8_t *difference, uint8_t *data, size_t data_length, uint8_t **shares, size_t data_blocks, size_t parity_blocks, uint8_t *erasures, uint8_t num_erasures);

#endif
