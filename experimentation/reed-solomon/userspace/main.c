#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rs.h"
#include <syscall.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>


uint32_t printConfig(struct config* conf){
	printf("num_data %d\n", conf->num_data);
	printf("num_entropy %d\n", conf->num_entropy);
	printf("num_carrier %d\n", conf->num_carrier);
	printf("polynomial_deg %d\n", 0);
	printf("k %d\n", conf->k);
	printf("n %d\n", conf->n);
	printf("total blocks %d\n", conf->total_blocks);
	printf("encode blocks %d\n", conf->encode_blocks);
	printf("block_portion %d\n", conf->block_portion);
	printf("padding %d\n", conf->padding);
	printf("block_size %d\n", conf->block_size);
	printf("final_padding %d\n", conf->final_padding);
	return 0;
}


int main(void){
	//initialize the variables
	init_rs(223);
	int i;
	double total_time;
	clock_t start, end;
	struct config *conf = malloc(sizeof(struct config));
	uint8_t **data = malloc(sizeof(uint8_t*));
	data[0] = malloc(4096);
	data[1] = malloc(4096);
	uint8_t **decoded_data = malloc(sizeof(uint8_t*));
	decoded_data[0] = malloc(4096);
	decoded_data[1] = malloc(4096);
	uint8_t **entropy = malloc(sizeof(uint8_t*));
	entropy[0] = malloc(4096);
	entropy[1] = malloc(4096);
	uint8_t **carrier = malloc(sizeof(uint8_t*));
	carrier[0] = malloc(4096);
	carrier[1] = malloc(4096);
	carrier[2] = malloc(4096);
	carrier[3] = malloc(4096);
	initialize(conf, 2, 2, 4);
	printConfig(conf);
	syscall(SYS_getrandom, data[0], 4096, 0);
	syscall(SYS_getrandom, data[1], 4096, 0);
	//hexDump("raw data", data, 8192);
	syscall(SYS_getrandom, entropy[0], 4096, 0);
	syscall(SYS_getrandom, entropy[1], 4096, 0);
	//hexDump("entropy", data, 4096);
	memset(carrier[0], 0, 4096);
	memset(carrier[1], 0, 4096);
	memset(carrier[2], 0, 4096);
	memset(carrier[3], 0, 4096);
	start = clock();
	int err = encode(conf, data, entropy, carrier);
	end = clock();
	total_time = ((double) (end - start)) / CLOCKS_PER_SEC;
	printf("Time to run encode %f\n", total_time);
	//hexDump("carrier", carrier, 4096);
	memset(decoded_data[0], 0, 4096);
	memset(decoded_data[1], 0, 4096);
	int err_loc[62];
	for(int i = 0; i < 62; i++){
		err_loc[i] = i;
	}
	start = clock();
	struct dm_afs_erasures *eras = malloc(sizeof(struct dm_afs_erasures));
	eras->codeword_size = 8;
	eras->num_erasures = 2;
	memset(eras->erasures, 0, 8);
	eras->erasures[0] = 1;
	eras->erasures[1] = 1;
	int errs = decode(conf, eras, decoded_data, entropy, carrier);
	end = clock();
	total_time = ((double) (end - start))/ CLOCKS_PER_SEC;
	printf("Time to run decode %f\n", total_time);
	//hexDump("data", decoded_data, 8192);
    int mismatch = 0;
    for(int i = 0; i < conf->num_data; i++){
	for(int j = 0; j < conf->block_size; j++){
        	if(decoded_data[i][j] != data[i][j]){
            		mismatch = 1;
        	}
	}
    }
    if (mismatch == 1){
        printf("houston we have a problem\n");
    }else{
        printf("decode successful\n");
    }
	return 0;
}
