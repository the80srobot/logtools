#include "common.h"
#include <string.h>

/* This is a really basic implementation of a linked list. It's used only to remember
 * a list of file paths passed as command line arguments and potentially other option
 * storage.
 */

typedef struct List *ListRef;

struct List {
  void *value;
  int free_value;
  ListRef next;
  ListRef prev;
};

#define LIST_APPEND(list, value) ((list == 0) ? list_make(value) : list_append(list, value))
#define LIST_APPEND_CPY(list, value) ((list == 0) ? list_make_cpy(value) : list_append_cpy(list, value))

static inline ListRef list_make(void *value) {
  ListRef list = (ListRef) xmalloc(sizeof(struct List));
  list->next = 0;
  list->prev = 0;
  list->value = value;
  list->free_value = 0;
  
  return list;
}

static inline ListRef list_make_cpy(char *value) {
  unsigned long len = strlen(value);
  char *cpy = (char *) xmalloc(len);
  strncpy(cpy, value, len);
  
  ListRef list = list_make((void *) cpy);
  list->free_value = 1;
  return list;
}

static inline ListRef list_append(ListRef list, void *value) {
  while(list->next != 0)
    list = list->next;
  
  list->next = (ListRef) xmalloc(sizeof(struct List));
  list->next->prev = list;
  list = list->next;
  list->value = value;
  list->next = 0;
  
  return list;
}

static inline ListRef list_append_cpy(ListRef list, const char *value) {
  unsigned long len = strlen(value);
  char *cpy = (char *) xmalloc(len);
  strncpy(cpy, value, len);
  
  list = list_append(list, (void *) cpy);
  list->free_value = 1;
  return list;
}

static inline void list_free(ListRef list) {
  if(!list)
    return;
  
  while (list->next != 0)
    list = list->next;
  
  ListRef tmp;
  while(list->prev != 0) {
    tmp = list;
    list = list->prev;
    if(tmp->free_value)
      free(tmp->value);
    free(tmp);
  }
  
  if(list->free_value)
    free(list->value);
  free(list);
}

static inline void list_each(ListRef list, void (*callback)(void *)) {
  if(!list)
    return;
  
  while(list->next != 0)
    list = list->next;
  
  do {
    callback(list->value);
  } while((list = list->prev));
}

static inline void list_each_ctx(ListRef list, void (*callback)(void *, void *), void *context) {
  if(!list)
    return;
  
  while(list->next != 0)
    list = list->next;
  
  do {
    callback(list->value, context);
  } while((list = list->prev));
}
