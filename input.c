#include "input.h"
#include "common.h"

/*
 * Return VAL aligned to the next multiple of ALIGNMENT.  VAL can be
 * an integer or a pointer. Both args must be free of side effects.
 */
#define ALIGN_TO(val, alignment) \
((size_t) (val) % (alignment) == 0 \
? (val) \
: (val) + ((alignment) - (size_t) (val) % (alignment)))

/* Globals */
unsigned char aio_eol = '\n';
size_t aio_pagesize = 0;

/* API */

aio_buffer *aio_buffer_alloc(void) {
  /* init on the first run */
  if(!aio_pagesize) {
      aio_pagesize = getpagesize();
      if(aio_pagesize <= 0) {
          fprintf(stderr, "aio_buffer: getpagesize either failed or returned something really stupid\n");
          exit(-1);
      }
  }
  
  aio_buffer *buffer = xmalloc(sizeof(aio_buffer));
  buffer->size = ALIGN_TO(AIO_BASE_BUFSIZE, aio_pagesize) + aio_pagesize + 1;
  buffer->data = xmalloc(buffer->size);
  buffer->fd = -1;
  
  return buffer;
}

void aio_buffer_free(aio_buffer *buffer) {
  if(buffer->fd != -1)
    close(buffer->fd);
  
  free(buffer->data);
  free(buffer);
}

void aio_buffer_close(aio_buffer *buffer) {
  if(buffer->fd != -1)
    close(buffer->fd);
  
  buffer->fd = -1;
}

int aio_buffer_open(aio_buffer *buffer, const char *path) {
  int fd = open(path, O_RDONLY);
  if(fd == -1)
    return AIO_ERROR_IO_READ_ERROR;
  
  return aio_buffer_init(buffer, fd);
}

int aio_buffer_init(aio_buffer *buffer, int fd) {
  if(buffer->fd != -1)
    close(buffer->fd);
  
  buffer->fd = fd;
  buffer->start = ALIGN_TO(buffer->data + 1, aio_pagesize);
  buffer->limit = buffer->size - (buffer->start - buffer->data);
  buffer->end = buffer->start + buffer->limit;
  buffer->start[-1] = aio_eol;
  
  buffer->linestart = buffer->start;
  buffer->linelimit = buffer->start - 1;
  
  off_t adjdump; /* this value will be discarded */
  return aio_buffer_fill(buffer, 0, &adjdump);
}

int aio_buffer_fill(aio_buffer *buffer, size_t keep, off_t *adjust) {
  /* calculate how much room there is for new data */
  size_t readsize = buffer->limit - keep;
  assert(readsize > 0);
  
  char *readstart = buffer->start + keep;
  char *keepstart = buffer->end - keep;
  *adjust = buffer->start - keepstart;
  
  /* move the saved memory to the start of the buffer */
  if(keep) {
    memmove(buffer->start, keepstart, keep);
  }
  
  ssize_t bytesread;
  while((bytesread = read(buffer->fd, readstart, readsize)) < 0 && errno == EINTR)
    continue;
  
  if(bytesread == 0)
    return AIO_ERROR_END_BUFFER;
  
  buffer->end = readstart + bytesread;
  
  return 0;
}

int aio_buffer_loadline(aio_buffer *buffer) {
  char *linestart = buffer->linestart;
  char *linelimit = buffer->linelimit;
  
  /* start at the next character after the limit */
  linestart = linelimit + 1;
  linelimit = linestart;
  
  off_t adjust = 0;
  int res = 0;
  
  for(; *linelimit != aio_eol; ++linelimit) {
    /* linelimit can be > buffer->end if previous buffer->linelimit == buffer->end - this is expected */
    if(linelimit >= buffer->end) {
      res = aio_buffer_fill(buffer, linelimit - linestart, &adjust);
      linestart += adjust;
      linelimit += adjust;
      if(res != 0)
        break;
    }
  }
  
  buffer->linestart = linestart;
  buffer->linelimit = linelimit;
  
  if(res != 0)
    return res;
  
  return 0;
}

void aio_buffer_writeline(aio_buffer *buffer, int fdout) {
  write(fdout, buffer->linestart, (size_t) (buffer->linelimit - buffer->linestart));
  write(fdout, "\n", 1);
}

int aio_buffer_setlinelimit(aio_buffer *buffer) {
  char *limit;
  off_t adj = 0;
  
  for(limit = buffer->linestart + 1; *limit != aio_eol; ++limit) {
    if(limit == buffer->end) {
      if(aio_buffer_fill(buffer, limit - buffer->linestart, &adj) != 0)
        break;
      buffer->linestart += adj;
      limit += adj;
    }
  }
  
  buffer->linelimit = limit;
  
  return 0;
}
