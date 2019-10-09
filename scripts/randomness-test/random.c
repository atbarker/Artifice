#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#define HISTOGRAM_SIZE 256
#define BLOCK_SIZE 4096
#define SIGNIFICANCE_LEVEL 293.247835 //p=0.05
#define ZERO_CHI 1044480.000000 //x^2 value for an all zero block

//calculate sum of (((observed-expected)^2)/expected) over a 256 element histogram
double chi_square(uint8_t *block) {
    int i = 0;
    double chi = 0;
    double expected = (double)BLOCK_SIZE/HISTOGRAM_SIZE;
    
    //buffer of longs for the histogram
    uint64_t histogram[HISTOGRAM_SIZE];
    //uint8_t zero_buffer[BLOCK_SIZE];
    memset(histogram, 0, HISTOGRAM_SIZE * sizeof(uint64_t));
    //memset(zero_buffer, 0, BLOCK_SIZE);
    //if (!memcmp(zero_buffer, block, BLOCK_SIZE)) {
    //    printf("block is zero\n");
    //} 

    //populate histogram
    for(i = 0; i < BLOCK_SIZE; i++) {
	histogram[block[i]]++;
    }
    //calculate chi
    for(i = 0; i < HISTOGRAM_SIZE; i++) {
        double numerator = histogram[i] - expected;
        chi += (numerator*numerator);
    }
    chi /= expected;

    //printf("Chi = %f\n", chi);
    
    return chi;
}

int is_block_psuedorandom(uint8_t *block) {
    if (chi_square(block) < SIGNIFICANCE_LEVEL){
        return 1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    const int arg_count = 2;
    int fd, i;
    size_t file_size, num_blocks;
    size_t random_blocks = 0, nonrandom_blocks = 0, zero_blocks = 0;
    uint8_t buffer[BLOCK_SIZE];

    if (argc != arg_count) {
        fprintf(stderr, "incorrect number of arguments");
	return -1;
    }

    fd = open(argv[1], O_RDONLY);
    lseek(fd, (size_t)0, SEEK_CUR);
    file_size = lseek(fd, (size_t)0, SEEK_END);
    lseek(fd, (size_t)0, SEEK_SET);
    num_blocks = file_size / BLOCK_SIZE;

    for (i = 0; i < num_blocks; i++) {
        read(fd, buffer, BLOCK_SIZE);
        if (chi_square(buffer) == ZERO_CHI) {
	    zero_blocks++;
            printf("block %d is zero'd\n", i);
        } else if (chi_square(buffer) < SIGNIFICANCE_LEVEL) {
	    random_blocks++;
	    printf("block %d is random\n", i);
	} else {
            nonrandom_blocks++;
	    printf("block %d is not random\n", i);
	}
    }

    fprintf(stdout, "Block device: %s, Random blocks %ld, Non random blocks %ld, total blocks %ld\n", argv[1], random_blocks, nonrandom_blocks, num_blocks);

    close(fd);
    return 0;
}
