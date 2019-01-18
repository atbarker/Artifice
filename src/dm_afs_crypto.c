/**
 * Author: Yash Gupta <ygupta@ucsc.edu>, Austen Barker <atbarker@ucsc.edu>
 * Copyright: UC Santa Cruz, SSRC
 */
#include <crypto/hash.h>
#include <dm_afs.h>
#include <dm_afs_modules.h>
#include <linux/crypto.h>
#include <linux/errno.h>
#include <linux/random.h>
#include <linux/rslib.h>

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
    afs_assert_action(!IS_ERR(tfm), ret = PTR_ERR(tfm), tfm_done, "could not allocate tfm [%d]", ret);

    desc = kmalloc(sizeof(*desc) + crypto_shash_descsize(tfm), GFP_KERNEL);
    afs_assert_action(desc, ret = -ENOMEM, desc_done, "could not allocate desc [%d]", ret);

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
    afs_assert_action(!IS_ERR(tfm), ret = PTR_ERR(tfm), tfm_done, "could not allocate tfm [%d]", ret);

    desc = kmalloc(sizeof(*desc) + crypto_shash_descsize(tfm), GFP_KERNEL);
    afs_assert_action(desc, ret = -ENOMEM, desc_done, "could not allocate desc [%d]", ret);

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
    afs_assert_action(!IS_ERR(tfm), ret = PTR_ERR(tfm), tfm_done, "could not allocate tfm [%d]", ret);

    desc = kmalloc(sizeof(*desc) + crypto_shash_descsize(tfm), GFP_KERNEL);
    afs_assert_action(desc, ret = -ENOMEM, desc_done, "could not allocate desc [%d]", ret);

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