
#include "mat_util.h"
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <openssl/conf.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/crypto.h>
#include <openssl/ssl.h>

/* Like pread(), but keep trying until everything has been read or we know for
 * sure that there was an error (or end-of-file) */
size_t
full_pread(int fd, void *buf, size_t count, off_t offset)
{
	ssize_t bytes_read;
	size_t bytes_remaining;

	for (bytes_remaining = count;
	     bytes_remaining != 0;
	     bytes_remaining -= bytes_read, buf += bytes_read,
	     	offset += bytes_read)
	{
		bytes_read = pread(fd, buf, bytes_remaining, offset);
		if (bytes_read <= 0) {
			if (bytes_read == 0)
				errno = EIO;
			else if (errno == EINTR)
				continue;
			break;
		}
	}
	return count - bytes_remaining;
}

/* Print an error message. */
void
fat_error(const char *format, ...)
{
	va_list va;

	fputs("mat-fuse: ", stderr);
	va_start(va, format);
	vfprintf(stderr, format, va);
	putc('\n', stderr);
	va_end(va);
}

/* Strip trailing spaces from a string */
void
remove_trailing_spaces(char *s)
{
	char *p = strchr(s, '\0');
	while (--p >= s && *p == ' ')
		*p = '\0';
}

/*obfuscate data when stored, performs the operation in place*/
int obfuscate_cluster(int entropy_fd, void *buf, size_t buf_size){

	unsigned char *entropy_buf = malloc(sizeof(unsigned char) * buf_size);
	unsigned char *user_buf = buf;
	int ret, i;

	ret = pread(entropy_fd, entropy_buf, buf_size, 0);
	if(!ret){
		fprintf(stderr, "Couldn't read in entropy data");
		return -1;
	}

	for(i = 0; i < buf_size; i++){
		user_buf[i] = user_buf[i] ^ entropy_buf[i];
	}
        
	free(entropy_buf);	
	return 0;
}

/*Generate cryptographic hash for block identification*/
u_int8_t* generate_id_hash(char *input, int hash_algo){

	SHA256_CTX ctx;
    u_int8_t* results = malloc(SHA256_DIGEST_LENGTH);
    int n;

    n = strlen(input);
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, (u_int8_t *)input, n);
    SHA256_Final(results, &ctx);
	return results;
}

/*Generate hash of the entire block for checksuming and identification*/
u_int8_t* generate_block_hash(void *buf, void *output, int hash_algo){
	
    SHA256_CTX ctx;
    u_int8_t results[SHA256_DIGEST_LENGTH];
    int n;

    n = strlen(buf);
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, (u_int8_t *)buf, n);
    SHA256_Final(results, &ctx);
	return results;
}
