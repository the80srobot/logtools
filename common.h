#include <stdlib.h>
#include <assert.h>

#ifndef COMMON
#define COMMON

#define VERSION_MAJOR 0
#define VERSION_RELEASE 9
#define VERSION_MINOR 0

/*
 * safe malloc
 */
inline static void *xmalloc(size_t size) {
    void *ptr = malloc(size);
    
    assert(size != 0);
    if(!ptr) {
        fprintf(stderr, "malloc call failed - this is pretty bad\n");
        exit(-2);
    }
    
    return ptr;
}

/*
 * safe realloc
 */
inline static void *xrealloc(void *ptr, size_t size) {
  ptr = realloc(ptr, size);
  
  assert(size != 0);
  if(!ptr) {
    fprintf(stderr, "realloc failed - that's not good at all\n");
    exit(-2);
  }
  
  return ptr;
}

#endif
