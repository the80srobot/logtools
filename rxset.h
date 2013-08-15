#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "common.h"
#include <assert.h>
#include <stdint.h>

#ifndef RXSET
#define RXSET

/* Note: the way these numbers are set up is important. */
#define RX_ERR_SEARCH_ERROR (-100)
#define RX_ERR_NOT_FOUND (-101)

#define RX_ERR_ADD_ERROR (-200)
#define RX_ERR_PARSE_ERROR (-201)
#define RX_ERR_DUPLICATE (-202)

#define RX_ERR_SUCCESS (0)

/* This opaque data type represents a set of search expressions.
 * All of the functions below operate on this type.
 */
typedef struct RXSet *RXSetRef;

/* This opaque data type represents a compiled search expression. */
typedef struct RXSearchTerm *RXSearchTermRef;

typedef void (*rx_freepayload_t)(void *);

 /* This struct is returned to represent results of various operations internally.
  * IMPORTANT: user must call rx_freeresult on the returned results.
  */
typedef struct {
  /* Relevant expression (or 0) - for searches this is
   * the left-most match; for errors the expression that caused
   * the error.
   */
  char *expression;
  /* Whatever you want it to be buddy. For search results this is the
   * pointer that was passed to rx_add as payload. For other results
   * this value is 0.
   */
  void *payload;
  /* An optional human-readable message describing the event. For
   * errors, a short explanation; for search results, it's currently 0.
   */
  char *msg;
  /* An error code guaranteed to be one of the RX_ERR_ defines above. */
  int err;
} RXResult;

/* These are used as template values so we don't need to initialize a new struct
 * just to report something we're likely to say repeatedly many times.
 * They're extern in case the user wants to examine them, but most likely
 * there will be no need to use these.
 */
extern const RXResult RXNotFound;
extern const RXResult RXSuccess;

typedef enum {
  /* All bytes are interpretted literally. */
  RXFormatLiteral = 0,
  /* The stream is interpretted as a regular expression. */
  RXFormatBasic = 1
} RXFormat;

/* Init a new set.
 * If you want to pass payloads to rx_add when adding search terms
 * then you should pass a pointer to a function that can free your payloads
 * to this function. Otherwise you may pass 0.
 */
RXSetRef rx_makeset(rx_freepayload_t);

/* Prepares the RXSet for use. Before this rx_compileset it's valid
 * to run rx_add against the set. After rx_compileset it's valid to
 * run rx_search.
 */
void rx_compileset(RXSetRef);

/* Free up all the memory used by the set. This might include some stuff pointed to
 * from returned RXResults, so be careful.
 */
void rx_freeset(RXSetRef);

/* This function must be called to free the memory allocated to display results
 * messages. If the user fails to call this function then the program will leak memory
 */
void rx_freeresult(RXResult);

/* Takes +length+ bytes from +bytes+ and interprets them as a search term. +format+
 * determines whether the characters are interpretted literally (RXFormatLiteral)
 * or parsed as a regular expression (RXFormatBasic). In the latter case
 * the return value might be something other than RXSuccess, but adding literal
 * expressions is guaranteed to succeed.
 *
 * Value passed as +payload+ will be returned in RXResults by subsequent rx_search calls.
 *
 * Note: this function can only be used with a set that has not had rx_compilest called on it.
 */
RXResult rx_add(RXSetRef, const char *bytes, size_t length, RXFormat format, void *payload);

/* Takes +length+ bytes from +bytes+ (assumed to be a single line) and looks for
 * any matches with the expressions that have been added to the set by previous rx_add calls.
 * returns RXNotFound in case of no matches; otherwise returns the left-most match.
 */
RXResult rx_search(const RXSetRef, const char *bytes, size_t length);

/* Counts the expressions that have been added to the set. Excludes duplicates. */
int rx_count(RXSetRef);

/* Dumps into STDOUT an internal representation of all searches as a graph in the .dot format.
 * Should work with Graphviz.
 */
void rx_dumpall(RXSetRef);

/* Dumps into STDOUT certain statistics about the complexity and state of the set. */
void rx_dumpinfo(RXSetRef);

/* Parses and dumps into STDOUT the internal representation of the expression.
 * Output in .dot format, compatible with Graphviz.
 */
void rx_dumpexpr(char *bytes, size_t length, RXFormat format);

/* Calls +callback+ for each search expression in the set, passing a search term object.
 * Currently the only use for this is to call rx_dumpterm.
 * To stop the iteration return 0 from the callback.
 */
void rx_eachterm(RXSetRef, int (*callback)(RXSearchTermRef));

/* Dumps into STDOUT the compiled search term in the same way as rx_dumpexpr.
 * Returns 0 so as to automatically terminate rx_eachterm.
 */
int rx_dumpterm(RXSearchTermRef);

#endif
