/* Global definitions for Reed-Solomon encoder/decoder
 * Phil Karn KA9Q, September 1996
 * Modified by Austen Barker 2018
 *
 * The parameters MM and KK specify the Reed-Solomon code parameters.
 *
 * Set MM to be the size of each code symbol in bits. The Reed-Solomon
 * block size will then be NN = 2**M - 1 symbols. Supported values are
 * defined in rs.c.
 *
 * Set KK to be the number of data symbols in each block, which must be
 * less than the block size. The code will then be able to correct up
 * to NN-KK erasures or (NN-KK)/2 errors, or combinations thereof with
 * each error counting as two erasures.
 */
#ifndef RS_H
#define RS_H

#include <linux/string.h>
#include <linux/slab.h>

#ifdef	MSDOS
#define	inline	/* broken MSC 5.0 */
#endif
#define MM  8		/* RS code over GF(2**MM) - change to suit */
#define KK 192 /* 223 */		/* KK = number of information symbols */

#define	NN ((1 << MM) - 1)

#if (MM <= 8)
typedef uint8_t dtype;
#else
typedef uint32_t dtype;
#endif

struct config{
    uint32_t num_data;
    uint32_t num_entropy;
    uint32_t num_carrier;
    uint32_t polynomial_deg;
    uint32_t k;
    uint32_t n;
    uint32_t total_blocks;
    uint32_t encode_blocks;
    uint32_t block_portion;
    uint32_t padding;
    uint32_t block_size;
    uint32_t final_padding;
};


//TODO find a better solution to handling the erasure location structures
//sadly right now limited to code words of length 32, theoretically capable of code words of length 255
struct dm_afs_erasures{
    uint8_t codeword_size;
    uint8_t num_erasures;
    uint8_t erasures[32];
};

/* Initialization function */
//TODO eliminate global variables and malloc here
void init_rs(uint32_t kk);

/* Cleanup function */
//TODO free a context structure here
void cleanup_rs(void);

/* These two functions *must* be called in this order (e.g.,
 * by init_rs()) before any encoding/decoding
 */

void generate_gf(void);	/* Generate Galois Field */
void gen_poly(int kk);	/* Generate generator polynomial */
void hexDump (char *desc, void *addr, uint32_t len);

uint32_t initialize(struct config* configuration, uint32_t num_data, uint32_t num_entropy, uint32_t num_carrier);
uint32_t encode(struct config* info, uint8_t** data, uint8_t** entropy, uint8_t** carrier);
uint32_t decode(struct config* info, struct dm_afs_erasures *erasures, uint8_t** data, uint8_t** entropy, uint8_t** carrier);

/* Reed-Solomon encoding
 * data[] is the input block, parity symbols are placed in bb[]
 * bb[] may lie past the end of the data, e.g., for (255,223):
 *	encode_rs(&data[0],&data[223]);
 */
uint32_t encode_rs(dtype* data, uint32_t kk, dtype* bb, uint32_t n_k);

/* Reed-Solomon erasures-and-errors decoding
 * The received block goes into data[], and a list of zero-origin
 * erasure positions, if any, goes in eras_pos[] with a count in no_eras.
 *
 * The decoder corrects the symbols in place, if possible and returns
 * the number of corrected symbols. If the codeword is illegal or
 * uncorrectible, the data array is unchanged and -1 is returned
 */
uint32_t eras_dec_rs(dtype data[], uint32_t* eras_pos, uint32_t kk, uint32_t no_eras);

#endif
