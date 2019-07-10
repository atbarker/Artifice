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
    desc->flags = 0;
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
    desc->flags = 0;
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
    desc->flags = 0;
    ret = crypto_shash_digest(desc, data, data_len, digest);
    afs_assert(!ret, compute_done, "error computing sha512 [%d]", ret);

compute_done:
    kfree(desc);

desc_done:
    crypto_free_shash(tfm);

tfm_done:
    return ret;
}
