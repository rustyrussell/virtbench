#ifndef _STDRUSTY_H
#define _STDRUSTY_H
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>

/* Is A == B ? */
#define streq(a,b) (strcmp((a),(b)) == 0)

/* Does A start with B ? */
#define strstarts(a,b) (strncmp((a),(b),strlen(b)) == 0)

/* Does A end in B ? */
static inline bool strends(const char *a, const char *b)
{
	if (strlen(a) < strlen(b))
		return false;

	return streq(a + strlen(a) - strlen(b), b);
}

#define ___stringify(x)	#x
#define __stringify(x)		___stringify(x)

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/* Upper bound to sprintf this simple type?  Each 3 bits < 1 digit. */
#define CHAR_SIZE(type) (((sizeof(type)*CHAR_BIT + 2) / 3) + 1)

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;

/* Convenient wrappers for malloc and realloc.  Use them. */
#define new(type) ((type *)malloc_nofail(sizeof(type)))
#define new_array(type, num) realloc_array((type *)0, (num))
#define realloc_array(ptr, num) ((__typeof__(ptr))_realloc_array((ptr), sizeof((*ptr)), (num)))

void *malloc_nofail(size_t size);
void *realloc_nofail(void *ptr, size_t size);
void *_realloc_array(void *ptr, size_t size, size_t num);

/* This version adds one byte (for nul term) */
void *grab_file(const char *filename, unsigned long *size);
void release_file(void *data, unsigned long size);

/* For writing daemons, based on Stevens. */
void daemonize(void);

/* Signal handling: returns fd to listen on. */
int signal_to_fd(int signal);
void close_signal(int fd);

/* from the Linux Kernel:
 * min()/max() macros that also do
 * strict type-checking.. See the
 * "unnecessary" pointer comparison.
 */
#define min(x,y) ({ \
	typeof(x) _x = (x);	\
	typeof(y) _y = (y);	\
	(void) (&_x == &_y);		\
	_x < _y ? _x : _y; })

#define max(x,y) ({ \
	typeof(x) _x = (x);	\
	typeof(y) _y = (y);	\
	(void) (&_x == &_y);		\
	_x > _y ? _x : _y; })

/* Delete (by memmove) elements of an array */
#define delete_arr(p, len, off, num) \
	_delete_arr((p), (len), (off), (num), sizeof(*p))
void _delete_arr(void *p, unsigned len, unsigned off, unsigned num, size_t s);

/* Write/read this much data. */
bool write_all(int fd, const void *data, unsigned long size);
bool read_all(int fd, void *data, unsigned long size);

bool is_dir(const char *dirname);

#endif /* _STDRUSTY_H */
