#include "rs.h"
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/slab.h>

static uint8_t gf_mul_table[256][256];

//NOTE all of this uses the primitive polynomial P(x) = x^8 + x^4 + x^3 + x^2 + 1 or in hex Ox11D
static uint8_t gf_exp[512] = {1, 2, 4, 8, 16, 32, 64, 128, 29, 58, 116, 232, 205, 135, 19, 38, 76, 152, 45, 90, 180, 117, 234, 201, 143, 3, 6, 12, 24, 48, 96, 192, 157, 39, 78, 156, 37, 74, 148, 53, 106, 212, 181, 119, 238, 193, 159, 35, 70, 140, 5, 10, 20, 40, 80, 160, 93, 186, 105, 210, 185, 111, 222, 161, 95, 190, 97, 194, 153, 47, 94, 188, 101, 202, 137, 15, 30, 60, 120, 240, 253, 231, 211, 187, 107, 214, 177, 127, 254, 225, 223, 163, 91, 182, 113, 226, 217, 175, 67, 134, 17, 34, 68, 136, 13, 26, 52, 104, 208, 189, 103, 206, 129, 31, 62, 124, 248, 237, 199, 147, 59, 118, 236, 197, 151, 51, 102, 204, 133, 23, 46, 92, 184, 109, 218, 169, 79, 158, 33, 66, 132, 21, 42, 84, 168, 77, 154, 41, 82, 164, 85, 170, 73, 146, 57, 114, 228, 213, 183, 115, 230, 209, 191, 99, 198, 145, 63, 126, 252, 229, 215, 179, 123, 246, 241, 255, 227, 219, 171, 75, 150, 49, 98, 196, 149, 55, 110, 220, 165, 87, 174, 65, 130, 25, 50, 100, 200, 141, 7, 14, 28, 56, 112, 224, 221, 167, 83, 166, 81, 162, 89, 178, 121, 242, 249, 239, 195, 155, 43, 86, 172, 69, 138, 9, 18, 36, 72, 144, 61, 122, 244, 245, 247, 243, 251, 235, 203, 139, 11, 22, 44, 88, 176, 125, 250, 233, 207, 131, 27, 54, 108, 216, 173, 71, 142, 1, 2, 4, 8, 16, 32, 64, 128, 29, 58, 116, 232, 205, 135, 19, 38, 76, 152, 45, 90, 180, 117, 234, 201, 143, 3, 6, 12, 24, 48, 96, 192, 157, 39, 78, 156, 37, 74, 148, 53, 106, 212, 181, 119, 238, 193, 159, 35, 70, 140, 5, 10, 20, 40, 80, 160, 93, 186, 105, 210, 185, 111, 222, 161, 95, 190, 97, 194, 153, 47, 94, 188, 101, 202, 137, 15, 30, 60, 120, 240, 253, 231, 211, 187, 107, 214, 177, 127, 254, 225, 223, 163, 91, 182, 113, 226, 217, 175, 67, 134, 17, 34, 68, 136, 13, 26, 52, 104, 208, 189, 103, 206, 129, 31, 62, 124, 248, 237, 199, 147, 59, 118, 236, 197, 151, 51, 102, 204, 133, 23, 46, 92, 184, 109, 218, 169, 79, 158, 33, 66, 132, 21, 42, 84, 168, 77, 154, 41, 82, 164, 85, 170, 73, 146, 57, 114, 228, 213, 183, 115, 230, 209, 191, 99, 198, 145, 63, 126, 252, 229, 215, 179, 123, 246, 241, 255, 227, 219, 171, 75, 150, 49, 98, 196, 149, 55, 110, 220, 165, 87, 174, 65, 130, 25, 50, 100, 200, 141, 7, 14, 28, 56, 112, 224, 221, 167, 83, 166, 81, 162, 89, 178, 121, 242, 249, 239, 195, 155, 43, 86, 172, 69, 138, 9, 18, 36, 72, 144, 61, 122, 244, 245, 247, 243, 251, 235, 203, 139, 11, 22, 44, 88, 176, 125, 250, 233, 207, 131, 27, 54, 108, 216, 173, 71, 142};

static uint8_t gf_log[256] = {0, 0, 1, 25, 2, 50, 26, 198, 3, 223, 51, 238, 27, 104, 199, 75, 4, 100, 224, 14, 52, 141, 239, 129, 28, 193, 105, 248, 200, 8, 76, 113, 5, 138, 101, 47, 225, 36, 15, 33, 53, 147, 142, 218, 240, 18, 130, 69, 29, 181, 194, 125, 106, 39, 249, 185, 201, 154, 9, 120, 77, 228, 114, 166, 6, 191, 139, 98, 102, 221, 48, 253, 226, 152, 37, 179, 16, 145, 34, 136, 54, 208, 148, 206, 143, 150, 219, 189, 241, 210, 19, 92, 131, 56, 70, 64, 30, 66, 182, 163, 195, 72, 126, 110, 107, 58, 40, 84, 250, 133, 186, 61, 202, 94, 155, 159, 10, 21, 121, 43, 78, 212, 229, 172, 115, 243, 167, 87, 7, 112, 192, 247, 140, 128, 99, 13, 103, 74, 222, 237, 49, 197, 254, 24, 227, 165, 153, 119, 38, 184, 180, 124, 17, 68, 146, 217, 35, 32, 137, 46, 55, 63, 209, 91, 149, 188, 207, 205, 144, 135, 151, 178, 220, 252, 190, 97, 242, 86, 211, 171, 20, 42, 93, 158, 132, 60, 57, 83, 71, 109, 65, 162, 31, 45, 67, 216, 183, 123, 164, 118, 196, 23, 73, 236, 127, 12, 111, 246, 108, 161, 59, 82, 41, 157, 85, 170, 251, 96, 134, 177, 187, 204, 62, 90, 203, 89, 95, 176, 156, 169, 160, 81, 11, 245, 22, 235, 122, 117, 44, 215, 79, 174, 213, 233, 230, 231, 173, 232, 116, 214, 244, 234, 168, 80, 88, 175};

Polynomial* init(uint8_t size, uint8_t length, uint8_t* byte_array){
    Polynomial *p = kmalloc(sizeof(Polynomial), GFP_KERNEL);
    p->size = size;
    p->array_length = length;
    p->byte_array = byte_array;
    return p;
}

Polynomial* new_poly(){
    Polynomial *p = kmalloc(sizeof(Polynomial), GFP_KERNEL);
    p->byte_array = kmalloc(GF_SIZE, GFP_KERNEL);
    memset(p->byte_array, 0, GF_SIZE);
    p->size = 0;
    p->array_length = GF_SIZE;
    return p;
}

//TODO:Something wrong here
void free_poly(Polynomial *p){
    kfree(p->byte_array);
    kfree(p);
}

int32_t append(Polynomial* p, uint8_t x){
    if(p->size + 1 <= p->array_length){
        p->byte_array[p->size] = x;
	p->size++;
	return 0;
    }else{
        return -1;
    }
}

int32_t reset(Polynomial* p){
    if(p){
        memset(p->byte_array, 0, p->size);
        return 0;
    }else{
        return -1;
    }
}

int32_t set(Polynomial* p, uint8_t* byte_seq, uint8_t size){
    if(p && byte_seq){
        memcpy(p->byte_array, byte_seq, size);
	p->size = size;
	return 0;
    }else{
        return -1;
    }
}

int32_t poly_copy(Polynomial* src, Polynomial* dest){
    if(src && dest){
        dest->size = src->size;
	dest->array_length = src->array_length;
        memcpy(dest->byte_array, src->byte_array, dest->array_length);
	return 0;
    }else{
        return -1;
    }
}

uint8_t length(Polynomial* p){
    return p->array_length;
}

uint8_t size(Polynomial* p){
    return p->size;
}

uint8_t value_at(Polynomial* p, uint32_t i){
    if(i <= p->size){
        return p->byte_array[i];
    }else{
        return 0;
    }
}

uint8_t* mem(Polynomial* p){
    return p->byte_array;
}

void print_polynomial(Polynomial* p){
    int i;
    printk(KERN_INFO "Polynomial size: %d\n", p->size);
    printk(KERN_INFO "[");
    for(i = 0; i < p->size; i++){
        printk(KERN_INFO " %d, ", p->byte_array[i]);
    }
    printk(KERN_INFO "]\n");
}

uint8_t mod(int32_t a, int32_t b){
   return (a % b + b) % b;
}

//performs galois field arithmetic between x and y, no lookup tables
uint8_t gf_mult(uint8_t x, uint8_t y, uint16_t prim_poly){
    uint8_t r = 0;

    while(y){
        if(y & 1){
            r = r ^ x;
        }
        y = y >> 1;
        x = x << 1;
        if((prim_poly > 0) && (x & GF_SIZE)){
            x = x ^ prim_poly;
        }
    }
    
    return r;
}

uint8_t gf_add(uint8_t x, uint8_t y){
    return x ^ y;
}

uint8_t gf_mult_lookup(uint8_t x, uint8_t y){
    if(x == 0 || y == 0){
        return 0;
    }
    return gf_exp[gf_log[x] + gf_log[y]];
}

uint8_t gf_mult_table(uint8_t x, uint8_t y){
    return gf_mul_table[x][y];
}

void populate_mult_lookup(){
    int i, j;
    for(i = 0; i < GF_SIZE; i++){
        for(j = 0; j < GF_SIZE; j++){
            gf_mul_table[i][j] = gf_mult_lookup(i, j);
        }
    }
}

uint8_t gf_div(int x, int y){
    //should be a divide by zero error, if we are dividing by zero just return 0
    if(y == 0) return 0;
    if(x == 0) return 0;
    return gf_exp[mod((gf_log[x] + MAX_VALUE - gf_log[y]), MAX_VALUE)];
}

uint8_t gf_pow(int x, int pow){
    return gf_exp[mod((gf_log[x] * pow), MAX_VALUE)];
}

uint8_t gf_inv(uint8_t x){
    return gf_exp[MAX_VALUE - gf_log[x]];
}

int32_t gf_poly_scalar(Polynomial *p, Polynomial *output, uint8_t scalar){
    int i;
    output->size = p->size;
    for(i = 0; i < p->size; i++){
        output->byte_array[i] = gf_mult_table(p->byte_array[i], scalar);
    }
    return 0;
}

int32_t gf_poly_add(Polynomial *a, Polynomial *b, Polynomial *output){
    int i;

    output->size = poly_max(a->size, b->size);
    memset(output->byte_array, 0, output->size);

    for(i = 0; i < a->size; i++){
        output->byte_array[i + output->size - a->size] = a->byte_array[i];
    }

    for(i = 0; i < b->size; i++){
        output->byte_array[i + output->size - b->size] ^= b->byte_array[i];
    }
    return 0;
}

int32_t gf_poly_mult(Polynomial *a, Polynomial *b, Polynomial *output){
    int i, j;
    output->size = a->size + b->size - 1;
    memset(output->byte_array, 0, output->size);
    for(j = 0; j < b->size; j++){
        for(i = 0; i < a->size; i++){
            output->byte_array[i+j] ^= gf_mult_table(a->byte_array[i], b->byte_array[j]);
        }
    }
    return 0;
}

int32_t gf_poly_div(Polynomial *a, Polynomial *b, Polynomial *output, Polynomial *remainder){
    uint8_t coef;
    int i, j;
    size_t sep;
    if(a != output){
        memcpy(output, a, a->size);
    }

    output->size = a->size;

    for(i = 0; i < (a->size - (b->size - 1)); i++){
        coef = output->byte_array[i];
	if(coef != 0){
	    for(j = 1; j < b->size; j++){
                if(b->byte_array[j] != 0){
                    output->byte_array[i+j] ^= gf_mult_table(b->byte_array[j], coef);
                }
            }
	}
    }
    sep = (b->size - 1);
    output->size = output->size - sep;
    remainder->size = sep;
    memcpy(remainder->byte_array, &output->byte_array[output->size], sep); 
    return 0;
}

uint8_t gf_poly_eval(Polynomial *p, uint8_t x){
    int i;
    uint8_t y = p->byte_array[0];
    for(i = 1; i < p->size; i++){
        y = gf_mult_table(y, x) ^ p->byte_array[i];
    }
    return y;
}

void rs_init(uint8_t parity_symbols){
    populate_mult_lookup();
    rs_generator_poly(parity_symbols);
}

void rs_generator_poly(uint8_t n_symbols){
    int i = 0;
    Polynomial *mulp = new_poly();
    Polynomial *temp = new_poly();

    gen_poly = new_poly();
    gen_poly->byte_array[0] = 1;
    gen_poly->size = 1;
    mulp->size = 2;

    for(i = 0; i < n_symbols; i++){
        mulp->byte_array[0] = 1;
	mulp->byte_array[1] = gf_pow(2, i);

	gf_poly_mult(gen_poly, mulp, temp);
        
	poly_copy(temp, gen_poly);
	reset(temp);
    }
    //free_poly(mulp);
    //free_poly(temp);
}

//input and output buffers are assumed to be populated
int encode(const void* data, uint8_t data_length, void* parity, uint8_t parity_length){
    Polynomial *output;
    int i, j;
    //temporary variable for encoding
    uint8_t coef = 0;

    //make sure we are in our block size limit for GF(2^8)
    if ((data_length + parity_length) >= GF_SIZE){
        printk(KERN_INFO "invalid data and parity sizes %d\n", data_length+parity_length);
	return -1;
    }
    output = new_poly();
    output->size = parity_length + data_length;
    memcpy(output->byte_array, data, data_length);
    
    for(i = 0; i < data_length; i++){
        coef = output->byte_array[i];
	if(coef != 0){
            for(j = 1; j < gen_poly->size; j++){
                output->byte_array[i+j] ^= gf_mult_table(gen_poly->byte_array[j], coef);
	    }
	}
    }
    memcpy(parity, &output->byte_array[data_length], parity_length);

    //free_poly(input);
    //free_poly(output);
    return 0;
}

Polynomial* calc_syndromes(Polynomial* message, uint8_t parity_length){
    Polynomial *syndromes = new_poly();
    int i;

    syndromes->size = parity_length+1;
    syndromes->byte_array[0] = 0;
    for(i = 1; i < parity_length+1; i++){
        syndromes->byte_array[i] = gf_poly_eval(message, gf_pow(2, i-1));
    }
    return syndromes;
}

Polynomial* find_error_locator(Polynomial *error_positions){
    Polynomial *error_loc, *mulp, *addp, *apol, *temp;
    int i;

    error_loc = new_poly();
    addp = new_poly();
    mulp = new_poly();
    temp = new_poly();
    apol = new_poly();
    error_loc->size = 1;
    error_loc->byte_array[0] = 1;
    mulp->size = 1;
    addp->size = 2;

    for(i = 0; i < error_positions->size; i++){
        mulp->byte_array[0] = 1;
	addp->byte_array[0] = gf_pow(2, error_positions->byte_array[i]);
	addp->byte_array[1] = 0;

	gf_poly_add(mulp, addp, apol);
	gf_poly_mult(error_loc, apol, temp);

	poly_copy(temp, error_loc);
    }

    //free_poly(addp);
    //free_poly(mulp);
    //free_poly(apol);
    //free_poly(temp);
    return error_loc;
}

//really just multiply the error locator by the syndromes and then reverse with division
Polynomial* find_error_evaluator(Polynomial* syndrome, Polynomial* error_loc, uint8_t parity_length){
    Polynomial* mulp = new_poly();
    Polynomial* divisor = new_poly();
    Polynomial* evaluator = new_poly();
    Polynomial* output = new_poly();

    gf_poly_mult(syndrome, error_loc, mulp);
    divisor->size = parity_length+2;
    reset(divisor);
    divisor->byte_array[0] = 1;

    gf_poly_div(mulp, divisor, output, evaluator);
    //free_poly(mulp);
    //free_poly(divisor);
    return evaluator;
}

Polynomial* correct_errors(Polynomial* syndromes, Polynomial* err_pos, Polynomial* message){
    Polynomial *c_pos, *error_loc, *corrected, *rsynd, *reval, *eval, *X, *mag, *err_loc_prime;
    int i, j;
    uint16_t l;
    uint8_t Xi_inv, err_loc_p, y;

    //have to set the error positions as if the last parity symbol is at the start of the codeword polynomial
    c_pos = new_poly();
    c_pos->size = err_pos->size;
    for(i = 0; i < err_pos->size; i++){
        c_pos->byte_array[i] = message->size - 1 - err_pos->byte_array[i];
    }

    //Calculate the error locator polynomial from the 
    error_loc = find_error_locator(c_pos);

    //Reverse the syndromes polynomial much like the error positions one
    rsynd = new_poly();
    rsynd->size = syndromes->size;
    for(i = syndromes->size-1, j = 0; i >= 0; i--, j++){
        rsynd->byte_array[j] = syndromes->byte_array[i];
    }

    //The evaluator spits out a reversed polynomial, we have to fix that
    reval = find_error_evaluator(rsynd, error_loc, error_loc->size-1);
    eval = new_poly();
    eval->size = reval->size;
    for(i = reval->size-1, j = 0; i >= 0; i--, j++){
        eval->byte_array[j] = reval->byte_array[i];
    }

    //This one stores the results of a Chien Search to produce a final error locator polynomial X
    X = new_poly();
    X->size = 0;
    for(i = 0; i < c_pos->size; i++){
        l = MAX_VALUE - c_pos->byte_array[i];
	append(X, gf_pow(2, -l));
    }


    //initialize the magnitude polynomial and the err_loc_prime placeholder
    mag = new_poly();
    reset(mag);
    mag->size = message->size;

    err_loc_prime = new_poly();
    //Forney's Algorithm
    for(i = 0; i < X->size; i++){
        Xi_inv = gf_inv(X->byte_array[i]);
	err_loc_prime->size = 0;
	for(j = 0; j < X->size; j++){
            if(j != i){
                append(err_loc_prime, gf_add(1, gf_mult_table(Xi_inv, X->byte_array[j])));
	    }
	}
	err_loc_p = 1;
	for(j = 0; j < err_loc_prime->size; j++){
            err_loc_p = gf_mult_table(err_loc_p, err_loc_prime->byte_array[j]);
        }
	y = gf_poly_eval(reval, Xi_inv);
	y = gf_mult_table(gf_pow(X->byte_array[i], 1), y);

	mag->byte_array[err_pos->byte_array[i]] = gf_div(y, err_loc_p);
    }

    corrected = new_poly();
    gf_poly_add(message, mag, corrected);
    /*free_poly(c_pos);
    free_poly(error_loc);
    free_poly(rsynd);
    free_poly(X);
    free_poly(reval);
    free_poly(eval);
    free_poly(mag);
    free_poly(err_loc_prime);*/
    return corrected;
}

int decode(const uint8_t* src, const uint8_t* parity, uint8_t data_size, uint8_t parity_size, uint8_t* dest, uint8_t* erasure_pos, uint8_t erasure_count){
    Polynomial *input_message = new_poly();
    Polynomial *decoded = new_poly();
    Polynomial *error_pos = new_poly();
    Polynomial *syndromes;


    input_message->size = data_size + parity_size;
    decoded->size = input_message->size;

    memcpy(input_message->byte_array, src, data_size);
    memcpy(&input_message->byte_array[data_size], parity, parity_size); 

    error_pos->size = erasure_count;
    set(error_pos, erasure_pos, erasure_count); 

    syndromes = calc_syndromes(input_message, parity_size);

    decoded = correct_errors(syndromes, error_pos, input_message);

    memcpy(dest, decoded->byte_array, decoded->size);
    return 0;
}

uint32_t initialize(struct config* configuration, uint32_t num_data, uint32_t num_entropy, uint32_t num_carrier){
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
    //TODO: change this out for a macro
	configuration->block_size = 4096;
	//how many encoding blocks do we need?
	configuration->encode_blocks = configuration->block_size / configuration->block_portion;
	//how much padding is needed for the last one
	configuration->final_padding = configuration->block_size % configuration->block_portion;
	//if an additional block is needed then we add to the number of encoding blocks
	if ((configuration->block_size % configuration->block_portion) != 0){
		configuration->encode_blocks++;
	}
	rs_init(configuration->n - configuration->k);
	return 0;
}


uint32_t dm_afs_encode(struct config* info, uint8_t** data, uint8_t** entropy, uint8_t** carrier){
	uint32_t i, j;
	uint32_t count = 0;
	uint32_t data_count = 0;
	uint32_t entropy_count = 0;
	uint32_t carrier_count = 0;
	uint8_t* encode_buffer = kmalloc(255, GFP_KERNEL);

	//handle all but the last encoding block, that one will likely have some sort of padding
	for(i = 0; i < info->encode_blocks-1; i++){
		memset(encode_buffer, 0, 255);
		for(j = 0; j < info->num_data; j++){	
			memcpy(&encode_buffer[count], &data[j][data_count], info->block_portion);
			count += info->block_portion;	
		}	
		for(j = 0; j < info->num_entropy; j++){
			memcpy(&encode_buffer[count], &entropy[j][entropy_count], info->block_portion);
			count += info->block_portion;
		}

		encode(encode_buffer, info->k, &encode_buffer[info->k], 255-info->k);
		for(j = 0; j < info->num_carrier; j++){
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
	for(i = 0; i < info->num_data; i++){
		memcpy(&encode_buffer[count], &data[i][data_count], info->final_padding);
		count += info->final_padding;
	}
	for(i = 0; i < info->num_entropy; i++){
		memcpy(&encode_buffer[count], &entropy[i][entropy_count], info->final_padding);
		count += info->final_padding;
	}
	encode(encode_buffer, info->k, &encode_buffer[info->k], 255-info->k);
	count = info->k;
	for(i = 0; i < info->num_carrier; i++){
		memcpy(&carrier[i][carrier_count], &encode_buffer[count], info->final_padding);
		count += info->final_padding;
	}
	
	kfree(encode_buffer);
	return 0;
}

uint32_t dm_afs_decode(struct config* info, struct dm_afs_erasures* erasures, uint8_t** data, uint8_t** entropy, uint8_t** carrier){
	uint32_t i, j;
	uint32_t errs = 0;
	uint32_t count = 0;
	uint32_t data_count = 0;
	uint32_t entropy_count = 0;
	uint32_t carrier_count = 0;
	uint8_t* decode_buffer = kmalloc(255, GFP_KERNEL);
	uint32_t eras = erasures->num_erasures * info->block_portion;
	uint32_t final_eras = (erasures->num_erasures * info->final_padding) + (255 - info->k - (info->num_carrier * info->final_padding));
	uint32_t *err_loc = kmalloc(sizeof(uint32_t) * eras, GFP_KERNEL);
	uint32_t *last_err_loc = kmalloc(sizeof(uint32_t) * final_eras, GFP_KERNEL);

	//generate our array for error locations
	for(i = 0; i < erasures->codeword_size; i++){
	    if(i < info->num_data || erasures->erasures[i] == 1){
                for(j = 0; j < info->block_portion; j++){
                    err_loc[(i * info->block_portion) + j] = (i * info->block_portion) + j;
		}
	    }
	}

	//handle all but the last encoding block
	for(i = 0; i < info->encode_blocks-1; i++){
		memset(decode_buffer, 0, 255);
		count = info->block_portion * info->num_data;
		//TODO only read in the entropy if needed and supplied
	        for(j = 0; j < info->num_entropy; j++){
			memcpy(&decode_buffer[count], &entropy[j][entropy_count], info->block_portion);
			count += info->block_portion;
		}
		for(j = 0; j < info->num_carrier; j++){
			memcpy(&decode_buffer[count], &carrier[j][carrier_count], info->block_portion);
			count += info->block_portion;
		}
		//errs = decode(decode_buffer, &decode_buffer[info->k], info->k, info->n - info->k, eras);
		if(errs == -1){
			//error out and return -1
			printk(KERN_INFO "Decode failed: %d\n", errs);
			return -1;
		}

		//new stuff
		for(j = 0; j< info->num_data; j++){
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
	for(i = 0; i < erasures->codeword_size; i++){
	      	if(i < info->num_data + info->num_entropy && erasures->erasures[i] == 1){
		    for(j = 0; j < info->final_padding; j++){
		        last_err_loc[(i * info->final_padding) + j] = (i * info->final_padding) + j;
		    }
		}else if(erasures->erasures[i] == 1){
		    for(j = 0; j < info->final_padding; j++){
		        last_err_loc[(i * info->final_padding) + j] = (i * info->final_padding) + j + info->k;
		    }
		}
	}
	j = 0;
	for(i = erasures->num_erasures * info->final_padding; i < final_eras; i++){
	    last_err_loc[i] = j + info->k + (info->num_carrier * info->final_padding);
	    j++;
	}

	//handle the last encoding block
	for(i = 0; i < info->num_entropy; i++){
                memcpy(&decode_buffer[count], &entropy[i][entropy_count], info->final_padding);
                count += info->final_padding;
        }
	count = info->k;
	for(i = 0; i < info->num_carrier; i++){
		memcpy(&decode_buffer[count], &carrier[i][carrier_count], info->final_padding);
		count += info->final_padding;
	}
	//errs = decode(decode_buffer, last_err_loc, info->k, final_eras);
	if(errs == -1){
		printk(KERN_INFO "problem found %d\n", errs);
	}
	count = 0;
	for(i = 0; i < info->num_data; i++){
		memcpy(&data[i][data_count], &decode_buffer[count], info->final_padding);
		count += info->final_padding;
	}

	kfree(err_loc);
	kfree(last_err_loc);
	kfree(decode_buffer);
	return 0;
}


