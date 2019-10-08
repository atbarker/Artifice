#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#define HISTOGRAM_SIZE 256
#define BLOCK_SIZE 4096
#define SIGNIFICANCE_LEVEL 293.247835

//calculate sum of (((observed-expected)^2)/expected) over a 256 element histogram
float chi_square(uint8_t *block) {
    int i = 0;
    float chi = 0;
    float expected = (float)input/HISTOGRAM_SIZE;
    
    //buffer of longs for the histogram
    int64_t *histogram = malloc(sizeof(int64_t) * HISTOGRAM_SIZE);

    //populate histogram
    for(i = 0; i < BLOCK_SIZE; i++) {
	histogram[buffer[i]]++;
    }
    //calculate chi
    for(i = 0; i < HISTOGRAM_SIZE; i++) {
        float numerator = histogram[i] - expected;
        chi += (numerator*numerator);
    }
    chi /= expected;

    free(histogram);

    return chi;
}

bool is_block_psuedorandom(uint8_t *block) {
    if (chi_square(block) < SIGNIFICANCE_LEVEL){
        return true;
    }
    return false;
}

uint8_t* read_block(char *file, size_t blocknum) {
    return NULL;
}

int main(int argc, char *argv[]) {
    return 0;
}
