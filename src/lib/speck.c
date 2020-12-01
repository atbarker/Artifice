#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "speck.h"

#define MAX64 -1UL
#define ROUNDS 34

uint64_t rotate(uint64_t in, int8_t rotation)
{
    if (rotation > 0) // left circular shift
    {
        return (in >> (64 - rotation)) | (in << rotation);
    }
    else // right circular shift
    {
        rotation = -1 * rotation;
        return (in << (64 - rotation)) | (in >> rotation);
    }
}

void add1(uint64_t * in, size_t length)
{
    int i = 0;
    do
    {
        if (in[i] == MAX64)
        {
            in[i] = 0x0UL;
            i++;
        }
        else
        {
            in[i] += 1;
            break;
        }
    } while (i < length);
}

void key_schedule(uint64_t *key, uint64_t *key_schedule)
{
    uint64_t l[3] = {key[1], key[2], key[3]};
    key_schedule[0] = key[0];

    for (int i = 0; i < ROUNDS - 1; i++)
    {
        l[i % 3] = (key_schedule[i] + rotate(l[i % 3], -8)) ^ (uint64_t)(i);
        key_schedule[i + 1] = rotate(key_schedule[i], 3) ^ l[i % 3];
    }

}

void enc_round(uint64_t *in, uint64_t key)
{
    uint64_t left = in[1];
    uint64_t right = in[0];

    in[1] = (rotate(left, -8) + right) ^ key;
    in[0] = rotate(right, 3) ^ (rotate(left, -8) + right) ^ key;
}

void speck_encrypt(uint64_t *in, uint64_t *out, uint64_t *key)
{
    out[0] = in[0];
    out[1] = in[1];
    uint64_t *keys = malloc(ROUNDS * sizeof(uint64_t));
    key_schedule(key, keys);

    for (int i = 0; i < ROUNDS; i++)
    {
        enc_round(out, keys[i]);
    }
    free(keys);
}

void dec_round(uint64_t *in, uint64_t key)
{
    uint64_t left = in[1];
    uint64_t right = in[0];

    in[1] = rotate((left ^ key) - rotate(left ^ right, -3), 8);
    in[0] = rotate(left ^ right, -3);
}

void speck_decrypt(uint64_t *in, uint64_t *out, uint64_t *key)
{
    out[0] = in[0];
    out[1] = in[1];
    uint64_t *keys = malloc(ROUNDS * sizeof(uint64_t));
    key_schedule(key, keys);

    for (int i = 0; i < ROUNDS; i++)
    {
        dec_round(out, keys[i]);
    }
    free(keys);
}

void speck_ctr(uint64_t *in, uint64_t *out, size_t pt_length, uint64_t *key, uint64_t *nonce)
{
    uint64_t* pad = malloc(2 * sizeof(uint64_t));
    uint64_t local_nonce[2] = {nonce[0], nonce[1]};

    for (int i = 0; i < pt_length; i+=2)
    {
        speck_encrypt(local_nonce, pad, key);

        out[i] = in[i] ^ pad[0];
        out[i + 1] = in[i + 1] ^ pad[1];

        add1(local_nonce, 2);
    }
    free(pad);
}

int main()
{
    uint64_t speck_key[4] = {0x0706050403020100UL,0x0f0e0d0c0b0a0908UL,0x1716151413121110UL, 0x1f1e1d1c1b1a1918UL};
    uint64_t speck_pt[2] = {0x202e72656e6f6f70UL, 0x65736f6874206e49UL};
    uint64_t speck_ct[2] = {0x4eeeb48d9c188f43UL, 0x4109010405c0f53eUL};
    uint64_t test[2] = {0UL, 0UL};

    speck_encrypt(speck_pt, test, speck_key);
    int speck_test = 1;
    for (int i = 0; i < 2; i++)
    {
        speck_test = speck_test && (speck_ct[i] == test[i]);
    }
    speck_decrypt(speck_ct, test, speck_key);
    for (int i = 0; i < 2; i++)
    {
        speck_test = speck_test && (speck_pt[i] == test[i]);
    }
    if (speck_test)
        printf("SPECK block cipher passed!\n");
    else
        printf("SPECK block cipher passed!\n");

    return 0;
}
