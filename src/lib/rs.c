/*
 * Reed-Solomon coding and decoding
 * Phil Karn (karn at ka9q.ampr.org) September 1996
 * Modified by Austen Barker (atbarker at ucsc.edu) 2018
 * 
 * This file is derived from the program "new_rs_erasures.c" by Robert
 * Morelos-Zaragoza (robert at spectra.eng.hawaii.edu) and Hari Thirumoorthy
 * (harit at spectra.eng.hawaii.edu), Aug 1995
 *
 * I've made changes to improve performance, clean up the code and make it
 * easier to follow. Data is now passed to the encoding and decoding functions
 * through arguments rather than in global arrays. The decode function returns
 * the number of corrected symbols, or -1 if the word is uncorrectable.
 *
 * This code supports a symbol size from 2 bits up to 16 bits,
 * implying a block size of 3 2-bit symbols (6 bits) up to 65535
 * 16-bit symbols (1,048,560 bits). The code parameters are set in rs.h.
 *
 * Note that if symbols larger than 8 bits are used, the type of each
 * data array element switches from unsigned char to unsigned int. The
 * caller must ensure that elements larger than the symbol range are
 * not passed to the encoder or decoder.
 *
 */
#include "lib/rs.h"
#include <linux/random.h>
#include <linux/string.h>

#if (KK >= NN)
#error "KK must be less than 2**MM - 1"
#endif

//#define ERASURE_DEBUG 1
//#define DEBUG 1

/* This defines the type used to store an element of the Galois Field
 * used by the code. Make sure this is something larger than a char if
 * if anything larger than GF(256) is used.
 *
 * Note: unsigned char will work up to GF(256) but int seems to run
 * faster on the Pentium.
 */
typedef int gf;

/* Primitive polynomials - see Lin & Costello, Error Control Coding Appendix A,
 * and  Lee & Messerschmitt, Digital Communication p. 453.
 */
#if (MM == 2) /* Admittedly silly */
int Pp[MM + 1] = { 1, 1, 1 };

#elif (MM == 3)
/* 1 + x + x^3 */
int Pp[MM + 1] = { 1, 1, 0, 1 };

#elif (MM == 4)
/* 1 + x + x^4 */
int Pp[MM + 1] = { 1, 1, 0, 0, 1 };

#elif (MM == 5)
/* 1 + x^2 + x^5 */
int Pp[MM + 1] = { 1, 0, 1, 0, 0, 1 };

#elif (MM == 6)
/* 1 + x + x^6 */
int Pp[MM + 1] = { 1, 1, 0, 0, 0, 0, 1 };

#elif (MM == 7)
/* 1 + x^3 + x^7 */
int Pp[MM + 1] = { 1, 0, 0, 1, 0, 0, 0, 1 };

#elif (MM == 8)
/* 1+x^2+x^3+x^4+x^8 */
int Pp[MM + 1] = { 1, 0, 1, 1, 1, 0, 0, 0, 1 };

#elif (MM == 9)
/* 1+x^4+x^9 */
int Pp[MM + 1] = { 1, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

#elif (MM == 10)
/* 1+x^3+x^10 */
int Pp[MM + 1] = { 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1 };

#elif (MM == 11)
/* 1+x^2+x^11 */
int Pp[MM + 1] = { 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1 };

#elif (MM == 12)
/* 1+x+x^4+x^6+x^12 */
int Pp[MM + 1] = { 1, 1, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 1 };

#elif (MM == 13)
/* 1+x+x^3+x^4+x^13 */
int Pp[MM + 1] = { 1, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1 };

#elif (MM == 14)
/* 1+x+x^6+x^10+x^14 */
int Pp[MM + 1] = { 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1 };

#elif (MM == 15)
/* 1+x+x^15 */
int Pp[MM + 1] = { 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 };

#elif (MM == 16)
/* 1+x+x^3+x^12+x^16 */
int Pp[MM + 1] = { 1, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1 };

#else
#error "MM must be in range 2-16"
#endif

/* Alpha exponent for the first root of the generator polynomial */
#define B0 1

/* index->polynomial form conversion table */
gf Alpha_to[NN + 1];

/* Polynomial->index form conversion table */
gf Index_of[NN + 1];

/* No legal value in index form represents zero, so
 * we need a special value for this purpose
 */
#define A0 (NN)

/* Generator polynomial g(x)
 * Degree of g(x) = 2*TT
 * has roots @**B0, @**(B0+1), ... ,@^(B0+2*TT-1)
 */
//gf Gg[NN - KK + 1];
gf *Gg;

/* Compute x % NN, where NN is 2**MM - 1,
 * without a slow divide
 */
static inline gf
modnn(int x)
{
    while (x >= NN) {
        x -= NN;
        x = (x >> MM) + (x & NN);
    }
    return x;
}

#define CLEAR(a, n)                     \
    {                                   \
        int ci;                         \
        for (ci = (n)-1; ci >= 0; ci--) \
            (a)[ci] = 0;                \
    }

#define COPY(a, b, n)                   \
    {                                   \
        int ci;                         \
        for (ci = (n)-1; ci >= 0; ci--) \
            (a)[ci] = (b)[ci];          \
    }
#define COPYDOWN(a, b, n)               \
    {                                   \
        int ci;                         \
        for (ci = (n)-1; ci >= 0; ci--) \
            (a)[ci] = (b)[ci];          \
    }

void
init_rs(uint32_t kk)
{
    Gg = kmalloc(sizeof(gf) * (NN - kk + 1), GFP_KERNEL);
    generate_gf();
    gen_poly(kk);
}

void
cleanup_rs()
{
    kfree(Gg);
}

void
hexDump(char *desc, void *addr, uint32_t len)
{
    uint32_t i;
    uint8_t buff[17];
    uint8_t *pc = (uint8_t *)addr;

    // Output description if given.
    if (desc != NULL)
        printk(KERN_INFO "%s:\n", desc);

    if (len == 0) {
        printk(KERN_INFO "  ZERO LENGTH\n");
        return;
    }
    if (len < 0) {
        printk(KERN_INFO "  NEGATIVE LENGTH: %i\n", len);
        return;
    }

    // Process every byte in the data.
    for (i = 0; i < len; i++) {
        // Multiple of 16 means new line (with line offset).

        if ((i % 16) == 0) {
            // Just don't print ASCII for the zeroth line.
            if (i != 0)
                printk(KERN_INFO "  %s\n", buff);

            // Output the offset.
            printk(KERN_INFO "  %04x ", i);
        }

        // Now the hex code for the specific character.
        printk(KERN_INFO " %02x", pc[i]);

        // And store a printable ASCII character for later.
        if ((pc[i] < 0x20) || (pc[i] > 0x7e))
            buff[i % 16] = '.';
        else
            buff[i % 16] = pc[i];
        buff[(i % 16) + 1] = '\0';
    }

    // Pad out last line if not exactly 16 characters.
    while ((i % 16) != 0) {
        printk(KERN_INFO "   ");
        i++;
    }

    // And print the final ASCII bit.
    printk(KERN_INFO "  %s\n", buff);
}

uint32_t
initialize(struct config *configuration, uint32_t num_data, uint32_t num_entropy, uint32_t num_carrier)
{
    configuration->total_blocks = num_data + num_entropy + num_carrier;
    configuration->num_data = num_data;
    configuration->num_entropy = num_entropy;
    configuration->num_carrier = num_carrier;
    //the amount of padding in each encoding block
    configuration->padding = 255 % configuration->total_blocks;
    //the amount of space in an encoding block for each data/entropy/carrier block
    configuration->block_portion = 255 / configuration->total_blocks;
    //the amount of space in the 255 symbol encode block to be used
    configuration->n = configuration->total_blocks * configuration->block_portion;
    //data + entropy symbols
    configuration->k = configuration->block_portion * (num_data + num_entropy);
    //how big is our FS block size
    configuration->block_size = 4096;
    //how many encoding blocks do we need?
    configuration->encode_blocks = configuration->block_size / configuration->block_portion;
    //how much padding is needed for the last one
    configuration->final_padding = configuration->block_size % configuration->block_portion;
    //if an additional block is needed then we add to the number of encoding blocks
    if ((configuration->block_size % configuration->block_portion) != 0) {
        configuration->encode_blocks++;
    }
    init_rs(configuration->k);
    return 0;
}

uint32_t
encode(struct config *info, uint8_t **data, uint8_t **entropy, uint8_t **carrier)
{
    uint32_t i, j;
    uint32_t count = 0;
    uint32_t data_count = 0;
    uint32_t entropy_count = 0;
    uint32_t carrier_count = 0;
    uint8_t *encode_buffer = kmalloc(255, GFP_KERNEL);

    //handle all but the last encoding block, that one will likely have some sort of padding
    for (i = 0; i < info->encode_blocks - 1; i++) {
        memset(encode_buffer, 0, 255);
        for (j = 0; j < info->num_data; j++) {
            memcpy(&encode_buffer[count], &data[j][data_count], info->block_portion);
            count += info->block_portion;
        }
        for (j = 0; j < info->num_entropy; j++) {
            memcpy(&encode_buffer[count], &entropy[j][entropy_count], info->block_portion);
            count += info->block_portion;
        }

        encode_rs(encode_buffer, info->k, &encode_buffer[info->k], 255 - info->k);
        for (j = 0; j < info->num_carrier; j++) {
            memcpy(&carrier[j][carrier_count], &encode_buffer[count], info->block_portion);
            count += info->block_portion;
        }
        carrier_count += info->block_portion;
        data_count += info->block_portion;
        entropy_count += info->block_portion;
        count = 0;
    }
    //When handling the last block move k to right after the entropy scraps

    memset(encode_buffer, 0, 255);
    for (i = 0; i < info->num_data; i++) {
        memcpy(&encode_buffer[count], &data[i][data_count], info->final_padding);
        count += info->final_padding;
    }
    for (i = 0; i < info->num_entropy; i++) {
        memcpy(&encode_buffer[count], &entropy[i][entropy_count], info->final_padding);
        count += info->final_padding;
    }
    encode_rs(encode_buffer, info->k, &encode_buffer[info->k], 255 - info->k);
    count = info->k;
    for (i = 0; i < info->num_carrier; i++) {
        memcpy(&carrier[i][carrier_count], &encode_buffer[count], info->final_padding);
        count += info->final_padding;
    }

    kfree(encode_buffer);
    return 0;
}

uint32_t
decode(struct config *info, struct dm_afs_erasures *erasures, uint8_t **data, uint8_t **entropy, uint8_t **carrier)
{
    uint32_t i, j;
    uint32_t errs = 0;
    uint32_t count = 0;
    uint32_t data_count = 0;
    uint32_t entropy_count = 0;
    uint32_t carrier_count = 0;
    uint8_t *decode_buffer = kmalloc(255, GFP_KERNEL);
    uint32_t eras = erasures->num_erasures * info->block_portion;
    uint32_t final_eras = (erasures->num_erasures * info->final_padding) + (255 - info->k - (info->num_carrier * info->final_padding));
    uint32_t *err_loc = kmalloc(sizeof(uint32_t) * eras, GFP_KERNEL);
    uint32_t *last_err_loc = kmalloc(sizeof(uint32_t) * final_eras, GFP_KERNEL);

    //generate our array for error locations
    for (i = 0; i < erasures->codeword_size; i++) {
        if (i < info->num_data || erasures->erasures[i] == 1) {
            for (j = 0; j < info->block_portion; j++) {
                err_loc[(i * info->block_portion) + j] = (i * info->block_portion) + j;
            }
        }
    }

    //handle all but the last encoding block
    for (i = 0; i < info->encode_blocks - 1; i++) {
        memset(decode_buffer, 0, 255);
        count = info->block_portion * info->num_data;
        //TODO only read in the entropy if needed and supplied
        for (j = 0; j < info->num_entropy; j++) {
            memcpy(&decode_buffer[count], &entropy[j][entropy_count], info->block_portion);
            count += info->block_portion;
        }
        for (j = 0; j < info->num_carrier; j++) {
            memcpy(&decode_buffer[count], &carrier[j][carrier_count], info->block_portion);
            count += info->block_portion;
        }
        errs = eras_dec_rs(decode_buffer, err_loc, info->k, eras);
        if (errs == -1) {
            //error out and return -1
            printk(KERN_INFO "Decode failed: %d\n", errs);
            return -1;
        }

        //new stuff
        for (j = 0; j < info->num_data; j++) {
            memcpy(&data[j][data_count], &decode_buffer[info->block_portion * j], info->block_portion);
        }
        carrier_count += info->block_portion;
        entropy_count += info->block_portion;
        data_count += info->block_portion;
        count = 0;
    }
    memset(decode_buffer, 0, 255);
    memset(err_loc, 0, errs);
    count = info->num_data * info->final_padding;

    //TODO check the number of erasures before decoding, needs to be rewritten
    j = 0;
    for (i = 0; i < erasures->codeword_size; i++) {
        if (i < info->num_data + info->num_entropy && erasures->erasures[i] == 1) {
            for (j = 0; j < info->final_padding; j++) {
                last_err_loc[(i * info->final_padding) + j] = (i * info->final_padding) + j;
            }
        } else if (erasures->erasures[i] == 1) {
            for (j = 0; j < info->final_padding; j++) {
                last_err_loc[(i * info->final_padding) + j] = (i * info->final_padding) + j + info->k;
            }
        }
    }
    j = 0;
    for (i = erasures->num_erasures * info->final_padding; i < final_eras; i++) {
        last_err_loc[i] = j + info->k + (info->num_carrier * info->final_padding);
        j++;
    }

    //handle the last encoding block
    for (i = 0; i < info->num_entropy; i++) {
        memcpy(&decode_buffer[count], &entropy[i][entropy_count], info->final_padding);
        count += info->final_padding;
    }
    count = info->k;
    for (i = 0; i < info->num_carrier; i++) {
        memcpy(&decode_buffer[count], &carrier[i][carrier_count], info->final_padding);
        count += info->final_padding;
    }
    errs = eras_dec_rs(decode_buffer, last_err_loc, info->k, final_eras);
    if (errs == -1) {
        printk(KERN_INFO "problem found %d\n", errs);
    }
    count = 0;
    for (i = 0; i < info->num_data; i++) {
        memcpy(&data[i][data_count], &decode_buffer[count], info->final_padding);
        count += info->final_padding;
    }

    kfree(err_loc);
    kfree(last_err_loc);
    kfree(decode_buffer);
    return 0;
}

/* generate GF(2**m) from the irreducible polynomial p(X) in p[0]..p[m]
   lookup tables:  index->polynomial form   alpha_to[] contains j=alpha**i;
                   polynomial form -> index form  index_of[j=alpha**i] = i
   alpha=2 is the primitive element of GF(2**m)
   HARI's COMMENT: (4/13/94) alpha_to[] can be used as follows:
        Let @ represent the primitive element commonly called "alpha" that
   is the root of the primitive polynomial p(x). Then in GF(2^m), for any
   0 <= i <= 2^m-2,
        @^i = a(0) + a(1) @ + a(2) @^2 + ... + a(m-1) @^(m-1)
   where the binary vector (a(0),a(1),a(2),...,a(m-1)) is the representation
   of the integer "alpha_to[i]" with a(0) being the LSB and a(m-1) the MSB. Thus for
   example the polynomial representation of @^5 would be given by the binary
   representation of the integer "alpha_to[5]".
                   Similarily, index_of[] can be used as follows:
        As above, let @ represent the primitive element of GF(2^m) that is
   the root of the primitive polynomial p(x). In order to find the power
   of @ (alpha) that has the polynomial representation
        a(0) + a(1) @ + a(2) @^2 + ... + a(m-1) @^(m-1)
   we consider the integer "i" whose binary representation with a(0) being LSB
   and a(m-1) MSB is (a(0),a(1),...,a(m-1)) and locate the entry
   "index_of[i]". Now, @^index_of[i] is that element whose polynomial 
    representation is (a(0),a(1),a(2),...,a(m-1)).
   NOTE:
        The element alpha_to[2^m-1] = 0 always signifying that the
   representation of "@^infinity" = 0 is (0,0,0,...,0).
        Similarily, the element index_of[0] = A0 always signifying
   that the power of alpha which has the polynomial representation
   (0,0,...,0) is "infinity".
 
*/

void
generate_gf(void)
{
    register int i, mask;

    mask = 1;
    Alpha_to[MM] = 0;
    for (i = 0; i < MM; i++) {
        Alpha_to[i] = mask;
        Index_of[Alpha_to[i]] = i;
        /* If Pp[i] == 1 then, term @^i occurs in poly-repr of @^MM */
        if (Pp[i] != 0)
            Alpha_to[MM] ^= mask; /* Bit-wise EXOR operation */
        mask <<= 1;               /* single left-shift */
    }
    Index_of[Alpha_to[MM]] = MM;
    /*
	 * Have obtained poly-repr of @^MM. Poly-repr of @^(i+1) is given by
	 * poly-repr of @^i shifted left one-bit and accounting for any @^MM
	 * term that may occur when poly-repr of @^i is shifted.
	 */
    mask >>= 1;
    for (i = MM + 1; i < NN; i++) {
        if (Alpha_to[i - 1] >= mask)
            Alpha_to[i] = Alpha_to[MM] ^ ((Alpha_to[i - 1] ^ mask) << 1);
        else
            Alpha_to[i] = Alpha_to[i - 1] << 1;
        Index_of[Alpha_to[i]] = i;
    }
    Index_of[0] = A0;
    Alpha_to[NN] = 0;
}

/*
 * Obtain the generator polynomial of the TT-error correcting, length
 * NN=(2**MM -1) Reed Solomon code from the product of (X+@**(B0+i)), i = 0,
 * ... ,(2*TT-1)
 *
 * Examples:
 *
 * If B0 = 1, TT = 1. deg(g(x)) = 2*TT = 2.
 * g(x) = (x+@) (x+@**2)
 *
 * If B0 = 0, TT = 2. deg(g(x)) = 2*TT = 4.
 * g(x) = (x+1) (x+@) (x+@**2) (x+@**3)
 */
void
gen_poly(int kk)
{
    register int i, j;

    Gg[0] = Alpha_to[B0];
    Gg[1] = 1; /* g(x) = (X+@**B0) initially */
    for (i = 2; i <= NN - kk; i++) {
        Gg[i] = 1;
        /*
		 * Below multiply (Gg[0]+Gg[1]*x + ... +Gg[i]x^i) by
		 * (@**(B0+i-1) + x)
		 */
        for (j = i - 1; j > 0; j--)
            if (Gg[j] != 0)
                Gg[j] = Gg[j - 1] ^ Alpha_to[modnn((Index_of[Gg[j]]) + B0 + i - 1)];
            else
                Gg[j] = Gg[j - 1];
        /* Gg[0] can never be zero */
        Gg[0] = Alpha_to[modnn((Index_of[Gg[0]]) + B0 + i - 1)];
    }
    /* convert Gg[] to index form for quicker encoding */
    for (i = 0; i <= NN - kk; i++)
        Gg[i] = Index_of[Gg[i]];
}

/*
 * take the string of symbols in data[i], i=0..(k-1) and encode
 * systematically to produce NN-KK parity symbols in bb[0]..bb[NN-KK-1] data[]
 * is input and bb[] is output in polynomial form. Encoding is done by using
 * a feedback shift register with appropriate connections specified by the
 * elements of Gg[], which was generated above. Codeword is   c(X) =
 * data(X)*X**(NN-KK)+ b(X)
 */
//TODO: perform length checks on data and bb
uint32_t
encode_rs(dtype *data, uint32_t kk, dtype *bb, uint32_t n_k)
{
    register int i, j;
    gf feedback;

    CLEAR(bb, NN - kk);
    for (i = kk - 1; i >= 0; i--) {
#if (MM != 8)
        if (data[i] > NN)
            return -1; /* Illegal symbol */
#endif
        feedback = Index_of[data[i] ^ bb[NN - kk - 1]];
        if (feedback != A0) { /* feedback term is non-zero */
            for (j = NN - kk - 1; j > 0; j--)
                if (Gg[j] != A0)
                    bb[j] = bb[j - 1] ^ Alpha_to[modnn(Gg[j] + feedback)];
                else
                    bb[j] = bb[j - 1];
            bb[0] = Alpha_to[modnn(Gg[0] + feedback)];
        } else { /* feedback term is zero. encoder becomes a
				 * single-byte shifter */
            for (j = NN - kk - 1; j > 0; j--)
                bb[j] = bb[j - 1];
            bb[0] = 0;
        }
    }
    return 0;
}

/*
 * Performs ERRORS+ERASURES decoding of RS codes. If decoding is successful,
 * writes the codeword into data[] itself. Otherwise data[] is unaltered.
 *
 * Return number of symbols corrected, or -1 if codeword is illegal
 * or uncorrectable.
 * 
 * First "no_eras" erasures are declared by the calling program. Then, the
 * maximum # of errors correctable is t_after_eras = floor((NN-KK-no_eras)/2).
 * If the number of channel errors is not greater than "t_after_eras" the
 * transmitted codeword will be recovered. Details of algorithm can be found
 * in R. Blahut's "Theory ... of Error-Correcting Codes".
 */
//TODO add in checks to make sure this doesn't freak out
uint32_t
eras_dec_rs(dtype data[NN], uint32_t *eras_pos, uint32_t kk, uint32_t no_eras)
{
    uint32_t deg_lambda, el, deg_omega;
    uint32_t i, j, r;
    gf u, q, tmp, num1, num2, den, discr_r;
    gf *recd = kmalloc(sizeof(int) * NN, GFP_KERNEL);
    gf lambda[NN - kk + 1], s[NN - kk + 1]; /* Err+Eras Locator poly
						 * and syndrome poly */
    gf b[NN - kk + 1], t[NN - kk + 1], omega[NN - kk + 1];
    gf root[NN - kk], reg[NN - kk + 1], loc[NN - kk];
    uint32_t syn_error, count;

    /* data[] is in polynomial form, copy and convert to index form */
    for (i = NN - 1; i >= 0; i--) {
#if (MM != 8)
        if (data[i] > NN)
            return -1; /* Illegal symbol */
#endif
        recd[i] = Index_of[data[i]];
    }
    /* first form the syndromes; i.e., evaluate recd(x) at roots of g(x)
	 * namely @**(B0+i), i = 0, ... ,(NN-kk-1)
	 */
    syn_error = 0;
    for (i = 1; i <= NN - kk; i++) {
        tmp = 0;
        for (j = 0; j < NN; j++)
            if (recd[j] != A0) /* recd[j] in index form */
                tmp ^= Alpha_to[modnn(recd[j] + (B0 + i - 1) * j)];
        syn_error |= tmp; /* set flag if non-zero syndrome =>
					 * error */
        /* store syndrome in index form  */
        s[i] = Index_of[tmp];
    }
    if (!syn_error) {
        /*
		 * if syndrome is zero, data[] is a codeword and there are no
		 * errors to correct. So return data[] unmodified
		 */
        return 0;
    }
    CLEAR(&lambda[1], NN - kk);
    lambda[0] = 1;
    if (no_eras > 0) {
        /* Init lambda to be the erasure locator polynomial */
        lambda[1] = Alpha_to[eras_pos[0]];
        for (i = 1; i < no_eras; i++) {
            u = eras_pos[i];
            for (j = i + 1; j > 0; j--) {
                tmp = Index_of[lambda[j - 1]];
                if (tmp != A0)
                    lambda[j] ^= Alpha_to[modnn(u + tmp)];
            }
        }
#ifdef ERASURE_DEBUG
        /* find roots of the erasure location polynomial */
        for (i = 1; i <= no_eras; i++)
            reg[i] = Index_of[lambda[i]];
        count = 0;
        for (i = 1; i <= NN; i++) {
            q = 1;
            for (j = 1; j <= no_eras; j++)
                if (reg[j] != A0) {
                    reg[j] = modnn(reg[j] + j);
                    q ^= Alpha_to[reg[j]];
                }
            if (!q) {
                /* store root and error location
				 * number indices
				 */
                root[count] = i;
                loc[count] = NN - i;
                count++;
            }
        }
        if (count != no_eras) {
            printk(KERN_INFO "\n lambda(x) is WRONG\n");
            return -1;
        }
#ifndef NO_PRINT
        printf("\n Erasure positions as determined by roots of Eras Loc Poly:\n");
        for (i = 0; i < count; i++)
            printk(KERN_INFO "%d ", loc[i]);
        printf("\n");
#endif
#endif
    }
    for (i = 0; i < NN - kk + 1; i++) {
        b[i] = Index_of[lambda[i]];
    }

    /*
	 * Begin Berlekamp-Massey algorithm to determine error+erasure
	 * locator polynomial
	 */
    r = no_eras;
    el = no_eras;
    while (++r <= NN - kk) { /* r is the step number */
        /* Compute discrepancy at the r-th step in poly-form */
        discr_r = 0;
        for (i = 0; i < r; i++) {
            if ((lambda[i] != 0) && (s[r - i] != A0)) {
                discr_r ^= Alpha_to[modnn(Index_of[lambda[i]] + s[r - i])];
            }
        }
        discr_r = Index_of[discr_r]; /* Index form */
        if (discr_r == A0) {
            /* 2 lines below: B(x) <-- x*B(x) */
            COPYDOWN(&b[1], b, NN - kk);
            b[0] = A0;
        } else {
            /* 7 lines below: T(x) <-- lambda(x) - discr_r*x*b(x) */
            t[0] = lambda[0];
            for (i = 0; i < NN - kk; i++) {
                if (b[i] != A0)
                    t[i + 1] = lambda[i + 1] ^ Alpha_to[modnn(discr_r + b[i])];
                else
                    t[i + 1] = lambda[i + 1];
            }
            if (2 * el <= r + no_eras - 1) {
                el = r + no_eras - el;
                /*
				 * 2 lines below: B(x) <-- inv(discr_r) *
				 * lambda(x)
				 */
                for (i = 0; i <= NN - kk; i++)
                    b[i] = (lambda[i] == 0) ? A0 : modnn(Index_of[lambda[i]] - discr_r + NN);
            } else {
                /* 2 lines below: B(x) <-- x*B(x) */
                COPYDOWN(&b[1], b, NN - kk);
                b[0] = A0;
            }
            COPY(lambda, t, NN - kk + 1);
        }
    }

    /* Convert lambda to index form and compute deg(lambda(x)) */
    deg_lambda = 0;
    for (i = 0; i < NN - kk + 1; i++) {
        lambda[i] = Index_of[lambda[i]];
        if (lambda[i] != A0)
            deg_lambda = i;
    }
    /*
	 * Find roots of the error+erasure locator polynomial. By Chien
	 * Search
	 */
    COPY(&reg[1], &lambda[1], NN - kk);
    count = 0; /* Number of roots of lambda(x) */
    for (i = 1; i <= NN; i++) {
        q = 1;
        for (j = deg_lambda; j > 0; j--)
            if (reg[j] != A0) {
                reg[j] = modnn(reg[j] + j);
                q ^= Alpha_to[reg[j]];
            }
        if (!q) {
            /* store root (index-form) and error location number */
            root[count] = i;
            loc[count] = NN - i;
            count++;
        }
    }

#ifdef DEBUG
    printk(KERN_INFO "\n Final error positions:\t");
    for (i = 0; i < count; i++)
        printk(KERN_INFO "%d ", loc[i]);
    printk(KERN_INFO "\n");
#endif
    if (deg_lambda != count) {
        /*
		 * deg(lambda) unequal to number of roots => uncorrectable
		 * error detected
		 */
        //printk(KERN_INFO "deg lambda off %d\n", deg_lambda);
        //printk(KERN_INFO "count %d\n", count);
        return -1;
    }
    /*
	 * Compute err+eras evaluator poly omega(x) = s(x)*lambda(x) (modulo
	 * x**(NN-KK)). in index form. Also find deg(omega).
	 */
    deg_omega = 0;
    for (i = 0; i < NN - kk; i++) {
        tmp = 0;
        j = (deg_lambda < i) ? deg_lambda : i;
        for (; j >= 0; j--) {
            if ((s[i + 1 - j] != A0) && (lambda[j] != A0))
                tmp ^= Alpha_to[modnn(s[i + 1 - j] + lambda[j])];
        }
        if (tmp != 0)
            deg_omega = i;
        omega[i] = Index_of[tmp];
    }
    omega[NN - kk] = A0;

    /*
	 * Compute error values in poly-form. num1 = omega(inv(X(l))), num2 =
	 * inv(X(l))**(B0-1) and den = lambda_pr(inv(X(l))) all in poly-form
	 */
    for (j = count - 1; j >= 0; j--) {
        num1 = 0;
        for (i = deg_omega; i >= 0; i--) {
            if (omega[i] != A0)
                num1 ^= Alpha_to[modnn(omega[i] + i * root[j])];
        }
        num2 = Alpha_to[modnn(root[j] * (B0 - 1) + NN)];
        den = 0;

        /* lambda[i+1] for i even is the formal derivative lambda_pr of lambda[i] */
        for (i = min(deg_lambda, (NN - kk - 1)) & ~1; i >= 0; i -= 2) {
            if (lambda[i + 1] != A0)
                den ^= Alpha_to[modnn(lambda[i + 1] + i * root[j])];
        }
        if (den == 0) {
#ifdef DEBUG
            printk(KERN_INFO "\n ERROR: denominator = 0\n");
#endif
            return -1;
        }
        /* Apply error to data */
        if (num1 != 0) {
            data[loc[j]] ^= Alpha_to[modnn(Index_of[num1] + Index_of[num2] + NN - Index_of[den])];
        }
    }
    kfree(recd);
    return count;
}
