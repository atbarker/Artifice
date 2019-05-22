#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// Use O_DIRECT.
#define __USE_GNU

// 1MB.
#define BUFFER_SIZE (1 << 20)

// Time.
#define INIT_TIME(x) _initTime(x)
#define GET_TIME(x) _getTime(x)

// Conversions.
#define TO_KB(x) (x / (1024.0))
#define TO_MB(x) (x / (1024.0 * 1024.0))
#define TO_GB(x) (x / (1024.0 * 1024.0 * 1024.0))

static inline void
_initTime(struct timespec *start)
{
    clock_gettime(CLOCK_MONOTONIC_RAW, start);
}

static inline double
_getTime(struct timespec start)
{
    double time_passed;
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    time_passed = (int64_t)1000000000L * (int64_t)(now.tv_sec - start.tv_sec);
    time_passed += (int64_t)(now.tv_nsec - start.tv_nsec);

    return time_passed / 1000000000.0;
}

enum {
    READ = 0,
    WRITE = 1,
    BOTH = 2
};

enum {
    SEQ = 0,
    RAND = 1
};

int fd;
int type;
double read_throughput = 0.0;
double write_throughput = 0.0;

void *
thread_write(void *t_arg)
{
    const int n_writes = 192;
    const int repeat_factor = 1;

    int urandom_fd;
    off_t offset;
    ssize_t ret;
    uint8_t buf[BUFFER_SIZE];
    double duration = 0.0;
    struct timespec time_ctx;
    int i, j;

    urandom_fd = open("/dev/urandom", O_RDONLY);
    if (urandom_fd < 0) {
        perror("[w] could not open file");
        return NULL;
    }

    ret = read(urandom_fd, buf, BUFFER_SIZE);
    if (ret != BUFFER_SIZE) {
        fprintf(stderr, "[w] could not read %u bytes from /dev/urandom\n", BUFFER_SIZE);
        return NULL;
    }

    for (i = 0; i < repeat_factor; i++) {
        fprintf(stderr, "Repeat: %d\n", i);

        INIT_TIME(&time_ctx);
        for (j = 0; j < n_writes; j++) {
            offset = (type == RAND) ? BUFFER_SIZE * (rand() % n_writes) : BUFFER_SIZE * j;
            ret = pwrite(fd, buf, BUFFER_SIZE, offset);
            fsync(fd);
        }
        duration += GET_TIME(time_ctx);
    }
    duration /= repeat_factor;

    write_throughput = (BUFFER_SIZE * n_writes) / duration;
    write_throughput = TO_MB(write_throughput);

    fprintf(stdout, "Write Throughput: %.4f MB/s\n", write_throughput);
    return NULL;
}

void *
thread_read(void *t_arg)
{
    const int n_reads = 192;
    const int repeat_factor = 1;

    off_t offset;
    ssize_t ret;
    uint8_t buf[BUFFER_SIZE];
    double duration = 0.0;
    struct timespec time_ctx;
    int i, j;

    for (i = 0; i < repeat_factor; i++) {
        fprintf(stderr, "Repeat: %d\n", i);

        INIT_TIME(&time_ctx);
        for (j = 0; j < n_reads; j++) {
            offset = (type == RAND) ? BUFFER_SIZE * (rand() % n_reads) : BUFFER_SIZE * j;
            ret = pread(fd, buf, BUFFER_SIZE, offset);
        }
        duration += GET_TIME(time_ctx);
    }
    duration /= repeat_factor;

    read_throughput = (BUFFER_SIZE * n_reads) / duration;
    read_throughput = TO_MB(read_throughput);

    fprintf(stdout, "Read Throughput: %.4f MB/s\n", read_throughput);
    return NULL;
}

int
main(int argc, char *argv[])
{
    const int arg_count = 4;

    pthread_t w_thread, r_thread;
    int operation;
    int ret;

    if (argc != arg_count) {
        fprintf(stderr, "incorrect number of arguments\n");
        return -1;
    }

    // Parse operation.
    if (!strcmp(argv[1], "r")) {
        operation = READ;
    } else if (!strcmp(argv[1], "w")) {
        operation = WRITE;
    } else if (!strcmp(argv[1], "rw")) {
        operation = BOTH;
    } else {
        fprintf(stderr, "incorrect operation: %s\n", argv[1]);
        return -1;
    }

    // Parse type.
    if (!strcmp(argv[2], "seq")) {
        type = SEQ;
    } else if (!strcmp(argv[2], "rand")) {
        type = RAND;
    } else {
        fprintf(stderr, "incorrect type: %s\n", argv[2]);
        return -1;
    }

    // Open disk/file.
    fd = open(argv[3], O_RDWR);
    //removed O_SYNC flag
    if (fd < 0) {
        perror("could not open file");
        return -1;
    }

    srand(time(0));
    if (operation == READ) {
        ret = pthread_create(&r_thread, NULL, thread_read, NULL);
        if (ret) {
            perror("[r] could not create thread");
            return -1;
        }
        pthread_join(r_thread, NULL);
    } else if (operation == WRITE) {
        ret = pthread_create(&w_thread, NULL, thread_write, NULL);
        if (ret) {
            perror("[w] could not create thread");
            return -1;
        }
        pthread_join(w_thread, NULL);
    } else {
        ret = pthread_create(&r_thread, NULL, thread_read, NULL);
        if (ret) {
            perror("[r] could not create thread");
            return -1;
        }

        ret = pthread_create(&w_thread, NULL, thread_write, NULL);
        if (ret) {
            perror("[w] could not create thread");
            return -1;
        }

        pthread_join(r_thread, NULL);
        pthread_join(w_thread, NULL);
    }

    close(fd);
    return 0;
}
