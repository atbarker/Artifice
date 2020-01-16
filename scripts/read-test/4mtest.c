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
 * Read 4MB of filler to argv[1].
 */
int
main(int argc, char *argv[])
{
    int src_fd, dest_fd;
    ssize_t ret;
    uint8_t buf[BUFFER_SIZE];
    int i;

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

    memset(buf, 1, BUFFER_SIZE);
    for (i = 0; i < 1024; i++) {
        ret = write(dest_fd, buf, BUFFER_SIZE);
        if (ret < BUFFER_SIZE) {
            perror("Could not write 4096 bytes");
            close(dest_fd);
            return -1;
        }
    }

    close(dest_fd);
    return 0;
}