#ifndef RS_H
#define RS_H

#include <linux/types.h>

#define poly_max(a, b) ((a > b) ? (a) : (b))

#define GF_SIZE 256
#define MAX_VALUE 255

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

struct dm_afs_erasures{
    uint8_t codeword_size;
    uint8_t num_erasures;
    uint8_t erasures[32];
};

typedef struct{
    int size; //size of the polynomial
    int array_length; //length of the byte array
    uint8_t* byte_array; //actual storage of the byte array for the polynomial
} Polynomial;


//assumes an already malloc'd byte array to the desired length (max should be 256 so 8 bits is enough)
Polynomial* init(uint8_t size, uint8_t length, uint8_t* byte_array);

//a new empty polynomial
Polynomial* new_poly(void);

//free memory allocated for a polynomial
void free_poly(Polynomial *p);

//append to an existing polynomial
int32_t append(Polynomial* p, uint8_t x);

//reset values to zero, really just zero the size, don't have to zero the memory
int32_t reset(Polynomial* p);

//set a polynomial to existing values
int32_t set(Polynomial* p, uint8_t* byte_seq, uint8_t size);

//make a copy of a polynomial
int32_t poly_copy(Polynomial* src, Polynomial* dest);

//return the length of the length of the byte array
uint8_t length(Polynomial* p);

//return the size of polynomial
uint8_t size(Polynomial* p);

//coefficient at position i
uint8_t value_at(Polynomial* p, uint32_t i);

//return pointer to byte array
uint8_t* mem(Polynomial* p);

void print_polynomial(Polynomial* p);


//The generator polynomial, only one instance of this
static Polynomial* gen_poly;

//Single number galois field functions
uint8_t init_tables(void);
uint8_t gf_add(uint8_t x, uint8_t y);
uint8_t gf_mult(uint8_t x, uint8_t y, uint16_t prim_poly);
uint8_t gf_mult_table(uint8_t x, uint8_t y);
void populate_mult_lookup(void);
uint8_t gf_mult_lookup(uint8_t x, uint8_t y);
uint8_t gf_div(int x, int y);
uint8_t gf_pow(int x, int pow);
uint8_t gf_inv(uint8_t x);

//polynomial galois field functions
int32_t gf_poly_scalar(Polynomial *p, Polynomial *output, uint8_t scalar);
int32_t gf_poly_add(Polynomial *a, Polynomial *b, Polynomial *output);
int32_t gf_poly_mult(Polynomial *a, Polynomial *b, Polynomial *output);
int32_t gf_poly_div(Polynomial *a, Polynomial *b, Polynomial *output, Polynomial *remainder);
uint8_t gf_poly_eval(Polynomial *p, uint8_t x);

//Reed-Solomon functions

//initialize tables and generator polynomial
void rs_init(uint8_t parity_symbols);

//generate the reed-solomon generator polynomial
void rs_generator_poly(uint8_t n_symbols);

//make sure that the two arrays are already allocated
int encode(const void* data, uint8_t data_length, void* parity, uint8_t parity_length);

//calculate the error syndromes
Polynomial* calc_syndromes(Polynomial* message, uint8_t parity_length);

//calculate the error locator polynomial
Polynomial* find_error_locator(Polynomial* error_positions);

//error evaluator polynomial
Polynomial* find_error_evaluator(Polynomial* synd, Polynomial* error_loc, uint8_t parity_length);

//calls find_error_locator and find_error_evaluator and computes the magnitude polynomial to correct errors
Polynomial* correct_errors(Polynomial* syn, Polynomial* err_pos, Polynomial* message);

//decode the message
int decode(const uint8_t* src, const uint8_t* parity, uint8_t data_size, uint8_t parity_size, uint8_t* dest, uint8_t* erasure_pos, uint8_t erasure_count);

uint32_t initialize(struct config* configuration, uint32_t num_data, uint32_t num_entropy, uint32_t num_carrier);
uint32_t dm_afs_encode(struct config* info, uint8_t** data, uint8_t** entropy, uint8_t** carrier);
uint32_t dm_afs_decode(struct config* info, struct dm_afs_erasures* erasures, uint8_t** data, uint8_t** entropy, uint8_t** carrier);

#endif
