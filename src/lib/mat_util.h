#ifndef _FAT_UTIL_H
#define _FAT_UTIL_H

#include "mat_types.h"
#include <sys/types.h>

extern size_t
full_pread(int fd, void *buf, size_t count, off_t offset);

extern void
fat_error(const char *format, ...);

extern void
remove_trailing_spaces(char *s);

extern int obfuscate_cluster(int entropy_fd, void *buf, size_t buf_size);
extern u_int8_t* generate_id_hash(char *input, int hash_algo);
extern u_int8_t* generate_block_hash(void *buf, void *output, int hash_algo);

#define min(a, b) ({__typeof__(a) _a = (a);\
		    __typeof__(b) _b = (b);\
		   _a < _b ? _a : _b; })

/**
 * container_of - cast a member of a structure out to the containing structure
 * @ptr:	the pointer to the member.
 * @type:	the type of the container struct this is embedded in.
 * @member:	the name of the member within the struct.
 *
 */
#define container_of(ptr, type, member) ({			\
	const __typeof__( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})

/* Bit-scan-reverse:  Return the position of the highest bit set in @n, indexed
 * from bit 0 as the low bit.  @n cannot be 0. */
static inline unsigned long
bsr(unsigned long n)
{
	__asm__("bsr %1,%0" : "=r" (n) : "rm" (n));
	return n;
}

/* Returns %true iff @n is a power of 2.  Zero is not a power of 2. */
static inline bool
is_power_of_2(size_t n)
{
	return (n & (n - 1)) == 0 && n != 0;
}

#ifndef NDEBUG
#  include <stdio.h>
#  include <errno.h>
#  define DEBUG(format, ...)					\
({								\
	int errno_save = errno;					\
	fprintf(stderr, "%s:%u %s(): ",				\
		  __FILE__, __LINE__, __func__);		\
	fprintf(stderr, format, ##__VA_ARGS__);			\
	putc('\n', stderr);					\
	errno = errno_save;					\
})
#else
#  define DEBUG(format, ...)
#endif

#endif /* _FAT_UTIL_H */
