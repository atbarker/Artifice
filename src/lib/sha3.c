#include "sha3.h"

#define R 1088

#define ROUNDS 24

#define SHA3_ROTL64(x, y) \
	(((x) << (y)) | ((x) >> ((sizeof(uint64_t)*8) - (y))))

static const uint64_t keccakf_rndc[24] = {
    0x0000000000000001, 0x0000000000008082, 0x800000000000808a,
    0x8000000080008000, 0x000000000000808b, 0x0000000080000001,
    0x8000000080008081, 0x8000000000008009, 0x000000000000008a,
    0x0000000000000088, 0x0000000080008009, 0x000000008000000a,
    0x000000008000808b, 0x800000000000008b, 0x8000000000008089,
    0x8000000000008003, 0x8000000000008002, 0x8000000000000080,
    0x000000000000800a, 0x800000008000000a, 0x8000000080008081,
    0x8000000000008080, 0x0000000080000001, 0x8000000080008008
};

static const unsigned keccakf_piln[24] = {
    10, 7, 11, 17, 18, 3, 5, 16, 8, 21, 24, 4, 15, 23, 19, 13, 12, 2, 20,
    14, 22, 9, 6, 1
};

static const unsigned keccakf_rotc[24] = {
    1, 3, 6, 10, 15, 21, 28, 36, 45, 55, 2, 14, 27, 41, 56, 8, 25, 43, 62,
    18, 39, 61, 20, 44
};

// f[1600] = p[1600, 24]
static void keccak_f(uint64_t s[25])
{
    int i, j;
    uint64_t t, bc[5];
    int round = 0;

    for(round = 0; round < ROUNDS; round++) {

        // theta
        for(i = 0; i < 5; i++)
            bc[i] = s[i] ^ s[i + 5] ^ s[i + 10] ^ s[i + 15] ^ s[i + 20];

        for(i = 0; i < 5; i++) {
            t = bc[(i + 4) % 5] ^ SHA3_ROTL64(bc[(i + 1) % 5], 1);
            for(j = 0; j < 25; j += 5)
                s[j + i] ^= t;
        }

        // rho + pi
        t = s[1];
        for(i = 0; i < 24; i++) {
            j = keccakf_piln[i];
            bc[0] = s[j];
            s[j] = SHA3_ROTL64(t, keccakf_rotc[i]);
            t = bc[0];
        }

        // chi
        for(j = 0; j < 25; j += 5) {
            for(i = 0; i < 5; i++)
                bc[i] = s[j + i];
            for(i = 0; i < 5; i++)
                s[j + i] ^= (~bc[(i + 1) % 5]) & bc[(i + 2) % 5];
        }

        // iota
        s[0] ^= keccakf_rndc[round];
    }
}

void sha3_256(uint8_t* message, uint64_t length, uint8_t* digest)
{
    int64_t bit_len = length * 8 + 2; // length in bytes to length in bits (+2 pad min)
    int64_t pad_len = ((-bit_len % R) + R) % R;
    int64_t num_blocks = (bit_len + pad_len) / 64;
    int64_t num_p = 0;
    uint64_t* P = NULL;
    uint64_t* d = NULL;
    int i, j;

    uint64_t* padded_message = sha_calloc(num_blocks, sizeof(uint64_t));
    memcpy(padded_message, message, length);

    // append 01
    padded_message[length / 8] |= 0x2UL << ((length % 8) * 8);
    // pad10*1
    padded_message[bit_len / 64] |= 0x1UL << (bit_len % 64);
    padded_message[(bit_len + pad_len - 1) / 64] |= 0x1UL << ((bit_len + pad_len - 1) % 64);

    num_p = num_blocks / 17;
    P = sha_calloc(num_p * 25, sizeof(uint64_t));

    for (i = 0; i < num_p; i++)
    {
        memcpy(P + i * 25, padded_message + i * 17, 17 * 8);
    }

    sha_free(padded_message);

    d = sha_calloc(25, sizeof(uint64_t));

    for (i = 0; i < num_p; i++)
    {
        for (j = 0; j < 25; j++)
        {
            d[j] ^= *(P + 25 * i + j);
        }

        keccak_f(d);
    }

    sha_free(P);
    memcpy(digest, d, 32);
    sha_free(d);
}

