/**
 * Author: Yash Gupta <ygupta@ucsc.edu>
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define BUFFER_SIZE 4096

/**
 * Read 4KB of '../src/dm_afs.c' and write it to file specified
 * in the argument.
 */
int
main(int argc, char *argv[])
{
    int src_fd, dest_fd;
    ssize_t ret;
    uint8_t buf[BUFFER_SIZE];

    // Read source file.
    src_fd = open("src/dm_afs.c", O_RDONLY);
    if (src_fd == -1) {
        perror("Could not open \'src/dm_afs.c\'");
        return -1;
    }

    memset(buf, 0, BUFFER_SIZE);
    ret = read(src_fd, buf, BUFFER_SIZE);
    if (ret < BUFFER_SIZE) {
        perror("Could not read 4096 bytes");
        close(src_fd);
        return -1;
    }
    close(src_fd);

    // Write output file.
    if (argc == 1) {
        dest_fd = 1;
    } else {
        dest_fd = open(argv[1], O_WRONLY);
        if (dest_fd == -1) {
            perror("Could not open writing file");
            return -1;
        }
    }

    ret = write(dest_fd, buf, BUFFER_SIZE);
    if (ret < BUFFER_SIZE) {
        perror("Could not write 4096 bytes");
    }

    close(dest_fd);
    return (ret == BUFFER_SIZE) ? 0 : -1;
}