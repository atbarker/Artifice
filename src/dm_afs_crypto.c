/**
 * Author: Yash Gupta <ygupta@ucsc.edu>, Austen Barker <atbarker@ucsc.edu>
 * Copyright: UC Santa Cruz, SSRC
 */
#include <crypto/hash.h>
#include <dm_afs.h>
#include <dm_afs_modules.h>
#include <dm_afs_crypto.h>
#include <linux/crypto.h>
#include <linux/errno.h>
#include <linux/random.h>
#include <linux/rslib.h>
#include <linux/types.h>

#define SPECK_BLOCK_SIZE 16

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
void speck_encrypt_128(uint64_t ct[2], uint64_t const pt[2], uint64_t const K[2])
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

void speck_128_hash(uint8_t *data, size_t data_length, uint8_t* hash){
    uint64_t seed[2] = {0,0};
    uint32_t rounds = data_length/SPECK_BLOCK_SIZE;
    uint64_t i, j, ctr[2], temp[2];

    j = 0;
    ctr[0] = 0;
    ctr[1] = 0;
    memset(hash, 0, 16);

    for(i = 0; i < rounds; i++){
        temp[0] = ((uint64_t *)data)[j + 0];
        temp[1] = ((uint64_t *)data)[j + 1];
        speck_encrypt_128(temp, ctr, seed);
        ((uint64_t*)hash)[0] ^= temp[0];
        ((uint64_t*)hash)[1] ^= temp[1];
        ctr[0]++;
        if(ctr[0] == 0) ctr[1]++;
        j += 2;
    }
}

/**
 * Intel implementation of CRC32 using the slicing-by-8 method
 * This uses lookup tables
 * Should pass in 0 for previous crc32
 * obtained from https://create.stephan-brumme.com/crc32/
 */
uint32_t gen_crc32(const void* data, size_t length, uint32_t previousCrc32)
{
  uint32_t *current_data = (uint32_t*)data;
  uint32_t crc = ~previousCrc32;
  uint8_t *currentChar = NULL;
  uint32_t one = 0;
  uint32_t two = 0;

  // process eight bytes at once
  while (length >= 8){
    one = *current_data++ ^ crc;
    two = *current_data++;
    crc = crc32Lookup[7][ one      & 0xFF] ^
          crc32Lookup[6][(one>> 8) & 0xFF] ^
          crc32Lookup[5][(one>>16) & 0xFF] ^
          crc32Lookup[4][ one>>24        ] ^
          crc32Lookup[3][ two      & 0xFF] ^
          crc32Lookup[2][(two>> 8) & 0xFF] ^
          crc32Lookup[1][(two>>16) & 0xFF] ^
          crc32Lookup[0][ two>>24        ];
    length -= 8;
  }
  currentChar = (uint8_t*) current_data;
  // remaining 1 to 7 bytes
  while (length--){
    crc = (crc >> 8) ^ crc32Lookup[0][(crc & 0xFF) ^ *currentChar++];
  }
  return ~crc;
}

/**
 * Somewhat fast CRC16 function
 */
uint16_t gen_crc16(const uint8_t *data, uint16_t size)
{
    uint16_t out = 0;
    int bits_read = 0, bit_flag;
    uint16_t crc = 0;
    int i = 0;
    int j = 0x0001;

    /* Sanity check: */
    if(data == NULL)
        return 0;

    while(size > 0)
    {
        bit_flag = out >> 15;

        /* Get next bit: */
        out <<= 1;
        out |= (*data >> bits_read) & 1; // item a) work from the least significant bits

        /* Increment bit counter: */
        bits_read++;
        if(bits_read > 7)
        {
            bits_read = 0;
            data++;
            size--;
        }

        /* Cycle check: */
        if(bit_flag)
            out ^= CRC16;

    }

    // item b) "push out" the last 16 bits
    for (i = 0; i < 16; ++i) {
        bit_flag = out >> 15;
        out <<= 1;
        if(bit_flag)
            out ^= CRC16;
    }

    // item c) reverse the bits
    crc = 0;
    i = 0x8000;
    for (; i != 0; i >>=1, j <<= 1) {
        if (i & out) crc |= j;
    }

    return crc;
}

bool check_crc32(uint32_t checksum, const void* data, size_t len){
    if(checksum == gen_crc32(data, len, 0)){
        return true;
    }
    return false;
}

bool check_crc16(uint16_t checksum, const void* data, size_t len){
    if(checksum == gen_crc16(data, len)){
        return true;
    }
    return false;
}

/**
 * Acquire a SHA1 hash of given data.
 * 
 * @digest Array to return digest into. Needs to be pre-allocated 20 bytes.
 */
int
hash_sha1(const void *data, const uint32_t data_len, uint8_t *digest)
{
    const char *alg_name = "sha1";
    struct crypto_shash *tfm;
    struct shash_desc *desc;
    int ret;

    tfm = crypto_alloc_shash(alg_name, 0, CRYPTO_ALG_ASYNC);
    afs_action(!IS_ERR(tfm), ret = PTR_ERR(tfm), tfm_done, "could not allocate tfm [%d]", ret);

    desc = kmalloc(sizeof(*desc) + crypto_shash_descsize(tfm), GFP_KERNEL);
    afs_action(desc, ret = -ENOMEM, desc_done, "could not allocate desc [%d]", ret);

    desc->tfm = tfm;
    ret = crypto_shash_digest(desc, data, data_len, digest);
    afs_assert(!ret, compute_done, "error computing sha1 [%d]", ret);

compute_done:
    kfree(desc);

desc_done:
    crypto_free_shash(tfm);

tfm_done:
    return ret;
}

/**
 * Acquire a SHA256 hash of given data.
 *
 * @digest Array to return digest into. Needs to be pre-allocated 32 bytes.
 */
int
hash_sha256(const void *data, const uint32_t data_len, uint8_t *digest)
{
    const char *alg_name = "sha256";
    struct crypto_shash *tfm;
    struct shash_desc *desc;
    int ret;

    tfm = crypto_alloc_shash(alg_name, 0, CRYPTO_ALG_ASYNC);
    afs_action(!IS_ERR(tfm), ret = PTR_ERR(tfm), tfm_done, "could not allocate tfm [%d]", ret);

    desc = kmalloc(sizeof(*desc) + crypto_shash_descsize(tfm), GFP_KERNEL);
    afs_action(desc, ret = -ENOMEM, desc_done, "could not allocate desc [%d]", ret);

    desc->tfm = tfm;
    ret = crypto_shash_digest(desc, data, data_len, digest);
    afs_assert(!ret, compute_done, "error computing sha256 [%d]", ret);

compute_done:
    kfree(desc);

desc_done:
    crypto_free_shash(tfm);

tfm_done:
    return ret;
}

/**
 * Acquire a SHA512 hash of given data.
 *
 * @digest Array to return digest into. Needs to be pre-allocated 64 bytes.
 */
int
hash_sha512(const void *data, const uint32_t data_len, uint8_t *digest)
{
    const char *alg_name = "sha512";
    struct crypto_shash *tfm;
    struct shash_desc *desc;
    int ret;

    tfm = crypto_alloc_shash(alg_name, 0, CRYPTO_ALG_ASYNC);
    afs_action(!IS_ERR(tfm), ret = PTR_ERR(tfm), tfm_done, "could not allocate tfm [%d]", ret);

    desc = kmalloc(sizeof(*desc) + crypto_shash_descsize(tfm), GFP_KERNEL);
    afs_action(desc, ret = -ENOMEM, desc_done, "could not allocate desc [%d]", ret);

    desc->tfm = tfm;
    ret = crypto_shash_digest(desc, data, data_len, digest);
    afs_assert(!ret, compute_done, "error computing sha512 [%d]", ret);

compute_done:
    kfree(desc);

desc_done:
    crypto_free_shash(tfm);

tfm_done:
    return ret;
}
