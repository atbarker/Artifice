#ifndef SPECK
#define SPECK

void speck_ctr(uint64_t *in, uint64_t *out, size_t pt_length, uint64_t *key, uint64_t *nonce);
void speck_encrypt(uint64_t *in, uint64_t *out, uint64_t *key);
void speck_decrypt(uint64_t *in, uint64_t *out, uint64_t *key);

#endif