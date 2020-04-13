#include "lib/cauchy_rs.h"
#include "lib/aont.h"

#define HASH_SIZE 32 

struct tcrypt_result {
    struct completion completion;
    int err;
};

struct skcipher_def {
    struct scatterlist sg;
    struct crypto_skcipher *tfm;
    struct skcipher_request *req;
    struct tcrypt_result result;
};

struct sdesc {
    struct shash_desc shash;
    char ctx[];
};

static int calc_hash(const uint8_t *data, size_t datalen, uint8_t *digest) {
    struct sdesc *sdesc;
    int ret;
    int size;
    struct crypto_shash *alg;
    char* hash_alg_name = "sha256";

    alg = crypto_alloc_shash(hash_alg_name, 0, 0);
    size = sizeof(struct shash_desc) + crypto_shash_descsize(alg);
    sdesc = kmalloc(size, GFP_KERNEL);
    if (!sdesc) {
        return PTR_ERR(sdesc);
    }
    sdesc->shash.tfm = alg;
    ret = crypto_shash_digest(&sdesc->shash, data, datalen, digest);
    kfree(sdesc);
    crypto_free_shash(alg);
    return ret;
}

/* Callback function */
static void test_skcipher_cb(struct crypto_async_request *req, int error)
{
    struct tcrypt_result *result = req->data;

    if (error == -EINPROGRESS)
        return;
    result->err = error;
    complete(&result->completion);
    pr_info("Encryption finished successfully\n");
}

/* Perform cipher operation */
static unsigned int test_skcipher_encdec(struct skcipher_def *sk,
                     int enc)
{
    int rc = 0;

    if (enc)
        rc = crypto_skcipher_encrypt(sk->req);
    else
        rc = crypto_skcipher_decrypt(sk->req);

    switch (rc) {
    case 0:
        break;
    case -EINPROGRESS:
    case -EBUSY:
        rc = wait_for_completion_interruptible(
            &sk->result.completion);
        if (!rc && !sk->result.err) {
            reinit_completion(&sk->result.completion);
            break;
        }
    default:
        pr_info("skcipher encrypt returned with %d result %d\n",
            rc, sk->result.err);
        break;
    }
    init_completion(&sk->result.completion);

    return rc;
}


/*
 *Because the Linux kernel crypto API is a place of nightmares
 *
 *Should operate of a 256 bit key to match the hash length
 */
int encrypt_payload(uint8_t *data, const size_t datasize, uint8_t *key, size_t keylength, int enc) {
    struct skcipher_def sk;
    struct crypto_skcipher *skcipher = NULL;
    struct skcipher_request *req = NULL;
    uint8_t ivdata[KEY_SIZE];
    int ret = -EFAULT;

    skcipher = crypto_alloc_skcipher("cbc-aes-aesni", 0, 0);
    if (IS_ERR(skcipher)) {
        pr_info("could not allocate skcipher handle\n");
        return PTR_ERR(skcipher);
    }

    req = skcipher_request_alloc(skcipher, GFP_KERNEL);
    if (!req) {
        pr_info("could not allocate skcipher request\n");
        ret = -ENOMEM;
        goto out;
    }

    skcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
                      test_skcipher_cb,
                      &sk.result);

    if (crypto_skcipher_setkey(skcipher, key, KEY_SIZE)) {
        pr_info("key could not be set\n");
        ret = -EAGAIN;
        goto out;
    }

    /* IV will be random */
    //get_random_bytes(ivdata, 16);
    memset(ivdata, 0, KEY_SIZE);

    sk.tfm = skcipher;
    sk.req = req;

    sg_init_one(&sk.sg, data, datasize);
    skcipher_request_set_crypt(req, &sk.sg, &sk.sg, KEY_SIZE, ivdata);
    init_completion(&sk.result.completion);

    ret = test_skcipher_encdec(&sk, enc);
    if (ret)
        goto out;

    pr_info("Encryption triggered successfully\n");

out:
    if (skcipher)
        crypto_free_skcipher(skcipher);
    if (req)
        skcipher_request_free(req);
    return ret;
}

//TODO change sizes here
int encode_aont_package(const uint8_t *data, size_t data_length, uint8_t **shares, size_t data_blocks, size_t parity_blocks){
    uint8_t canary[CANARY_SIZE];
    size_t cipher_size = data_length + CANARY_SIZE;
    size_t encrypted_payload_size = cipher_size + KEY_SIZE;
    size_t rs_block_size = encrypted_payload_size / data_blocks;
    uint8_t key[KEY_SIZE];
    uint8_t hash[HASH_SIZE];
    cauchy_encoder_params params;
    uint8_t *encode_buffer = kmalloc(encrypted_payload_size, GFP_KERNEL);
    int i = 0;
    
    //TODO Compute canary of the data block (small hash?)
    memset(canary, 0, CANARY_SIZE);
    memcpy(encode_buffer, data, data_length);
    memcpy(encode_buffer, canary, CANARY_SIZE);

    //generate key and IV
    get_random_bytes(key, sizeof(key)); 
    encrypt_payload(encode_buffer, cipher_size, key, KEY_SIZE, 1);

    params.BlockBytes = rs_block_size;
    params.OriginalCount = data_blocks;
    params.RecoveryCount = parity_blocks;

    calc_hash(encode_buffer, cipher_size, hash);

    for (i = 0; i < KEY_SIZE; i++) {
        encode_buffer[cipher_size + i] = key[i] ^ hash[i];
    }
    //TODO eliminate these memcpy operations, do everything in place
    for (i = 0; i < data_blocks; i++) {
        memcpy(shares[i], &encode_buffer[rs_block_size * i], rs_block_size);
    }
    
    cauchy_rs_encode(params, shares, &shares[data_blocks]);
    
    kfree(encode_buffer);
    return 0;
}

int decode_aont_package(uint8_t *data, size_t data_length, uint8_t **shares, size_t data_blocks, size_t parity_blocks, uint8_t *erasures, uint8_t num_erasures){
    uint8_t canary[CANARY_SIZE];
    size_t cipher_size = data_length + CANARY_SIZE;
    size_t encrypted_payload_size = cipher_size + KEY_SIZE;
    size_t rs_block_size = encrypted_payload_size / data_blocks;
    uint8_t key[KEY_SIZE];
    uint8_t hash[HASH_SIZE];
    cauchy_encoder_params params;
    uint8_t *encode_buffer = kmalloc(encrypted_payload_size, GFP_KERNEL);
    int ret;
    int i;

    memset(canary, 0, CANARY_SIZE);

    params.BlockBytes = rs_block_size;
    params.OriginalCount = data_blocks;
    params.RecoveryCount = parity_blocks;

    ret = cauchy_rs_decode(params, shares, &shares[data_blocks], erasures, num_erasures);

    for(i = 0; i < data_blocks; i++){
        memcpy(&encode_buffer[rs_block_size * i], shares[i], rs_block_size);
    }

    calc_hash(encode_buffer, cipher_size, hash);

    for(i = 0; i < KEY_SIZE; i++){
        key[i] = encode_buffer[cipher_size + i] ^ hash[i];
    }

    encrypt_payload(encode_buffer, cipher_size, key, KEY_SIZE, 0);
    if(memcmp(canary, &encode_buffer[data_length], CANARY_SIZE)){
        return -1;
    }
    memcpy(data, encode_buffer, data_length);

    kfree(encode_buffer);
    return 0;
}
