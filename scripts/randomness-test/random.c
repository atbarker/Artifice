#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>

#define HISTOGRAM_SIZE 256
#define BLOCK_SIZE 4096
#define SIGNIFICANCE_LEVEL 293.247835

//calculate sum of (((observed-expected)^2)/expected) over a 256 element histogram
float chi_square(uint8_t *block) {
    int i = 0;
    float chi = 0;
    float expected = (float)BLOCK_SIZE/HISTOGRAM_SIZE;
    
    //buffer of longs for the histogram
    uint64_t *histogram = malloc(sizeof(uint64_t) * HISTOGRAM_SIZE);

    //populate histogram
    for(i = 0; i < BLOCK_SIZE; i++) {
	histogram[block[i]]++;
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

int main(int argc, char *argv[]) {
    const int arg_count = 2;
    int fd, i;
    size_t file_size, num_blocks;
    size_t random_blocks = 0, nonrandom_blocks = 0;
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
        if (is_block_psuedorandom(buffer)) {
	    random_blocks++;
	} else {
            nonrandom_blocks++;
	}
    }

    fprintf(stdout, "Block device: %s, Random blocks %ld, Non random blocks %ld, total blocks %ld\n", argv[1], random_blocks, nonrandom_blocks, num_blocks);

    close(fd);
    return 0;
}
