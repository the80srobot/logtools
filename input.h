/* This is a somewhat reusable buffering library intended for use anywhere where
 * we primarily read from STDIN. Performance for reading plain files is not currently
 * as good as it could be (although it's still much better than stdio.h).
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#ifndef AIO_BUFFER
#define AIO_BUFFER

#define AIO_BASE_BUFSIZE 32768

extern size_t aio_pagesize; /* memory page alignment */
extern unsigned char aio_eol;

#define AIO_ERROR_LINE_LONGER_THAN_BUFSIZE (-7001)
#define AIO_ERROR_LINE_ZERO_LENGTH (-7002)
#define AIO_ERROR_IO_READ_ERROR (-7101)
#define AIO_ERROR_BUFFER_FILL_FAIL (-7200)
#define AIO_ERROR_END_BUFFER (-7300)

/*
 * The allocated memory looks like this:
 * |.data|__<PADDING>__|.start|__<USER DATA>__|.end = .start + .limit|__<PADDING>__|.data + .size|
 */
typedef struct {
  char *data; /* start of the allocated buffer */
  size_t size; /* allocated size - this is different than BASE_BUFSIZE because of page alignment */
  
  char *start; /* start of user-visible data */
  char *end; /* end of user-visible data */
  size_t limit; /* count of bytes from start to end */
  
  int fd; /* input descriptor for read calls */
  
  char *linestart; /* location of the latest newline seen */
  char *linelimit; /* location of the next newline after linestart */
} aio_buffer;

/* safely allocates a new buffer and sets pagesize */
aio_buffer *aio_buffer_alloc(void);

void aio_buffer_free(aio_buffer *buffer);

/* Sets the file descriptor to fd and fills the buffer for the first time.
 * Other initialization code is also run to ensure the buffer is pristine.
 * This function is safe to call multiple times on the same buffer with different file
 * descriptors (for buffer reuse).
 */
int aio_buffer_init(aio_buffer *buffer, int fd);
void aio_buffer_close(aio_buffer *buffer);
int aio_buffer_open(aio_buffer *buffer, const char *path);
int aio_buffer_fill(aio_buffer *buffer, size_t keep, off_t *adjust);
int aio_buffer_loadline(aio_buffer *buffer);
int aio_buffer_setlinelimit(aio_buffer *buffer);
void aio_buffer_writeline(aio_buffer *buffer, int fdout);

#endif
