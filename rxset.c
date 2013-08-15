#include "rxset.h"
#include <stdint.h>
#include <stdarg.h>

const RXResult RXNotFound = (RXResult) {0,0,0,RX_ERR_NOT_FOUND};
const RXResult RXSuccess = (RXResult) {0,0,0,RX_ERR_SUCCESS};

typedef unsigned char byte_t;

/* This value represents a single symbol in the expression.
 * bits:
 * ---- BASE 16 bits
 * | 1..8 - literal byte values
 * | 9..16 - special values (character classes and wildcards)
 * ---- EXTENDED 16 bits
 * | 17..24 - flags (such as kleene star and plus)
 * | 25..32 - reserved
 * ----
 */

typedef uint32_t symbol_t;
typedef symbol_t *expr_t;

#define SHIFT_CLASS_ANY (1 << 9)
#define SHIFT_CLASS_DIGIT (1 << 10)
#define SHIFT_CLASS_LETTER (1 << 11)
#define SHIFT_CLASS_SPACE (1 << 12)

#define SHIFT_FLAG_KSTAR (1 << 17)
#define SHIFT_FLAG_KCROSS (1 << 18)
#define SHIFT_FLAG_QUESTION (1 << 19)


typedef struct Tree *TreeRef;

struct RXSet {
  TreeRef root;
  int mutable;
  unsigned count_expr;
  unsigned count_node;
  rx_freepayload_t freepayload;
};

/* This struct is added to Tree nodes which represent an end of a
 * search term. It contains the information needed to recognize
 * which expression triggered the match, returning it in a
 * human-readable form, and the void* pointer to payload which
 * the user passed to rx_add when adding the expression.
 */
struct RXSearchTerm {
  expr_t expr;
  void *payload;
  char *original;
};

/* We use a ternary search tree to represent the trie. This
 * provides reasonable footprint for our branching factor requirements
 * and decent performance. When the tree is being constructed
 * only right and middle branches are occupied, effectively creating
 * a linked list of child trie nodes at every trie node. Before first
 * use, the tree is optimized with a single pass of a modified DSW
 * balancing algorithm (see http://penguin.ewu.edu/~trolfe/DSWpaper/)
 * after which it becomes immutable.
 */
struct Tree {
  symbol_t symbol;
  /* 0 = left; 1 = right; 2 = middle */
  TreeRef links[3];
  /* When non-zero then this node represents the last byte in a word. */
  RXSearchTermRef accepting_term;
};

/* Private API */

static inline TreeRef rx_maketree() {
  TreeRef _tree = (TreeRef) xmalloc(sizeof(struct Tree));
  
  _tree->symbol = 0;
  _tree->accepting_term = 0;
  _tree->links[0] = 0;
  _tree->links[1] = 0;
  _tree->links[2] = 0;
  
  return _tree;
}

static char *rx_newmsg(const char *format, ...) {
  va_list vl;
  va_start(vl, format);
  size_t length = vsnprintf(0, 0, format, vl) + 1;
  assert(length > 1);
  va_end(vl);
  
  va_start(vl, format);
  char *buffer = (char *) xmalloc(length);
  vsnprintf(buffer, length, format, vl);
  va_end(vl);
  
  return buffer;
}

static RXResult rx_compileexpr_lit(char *original, size_t length, expr_t *expr_buf) {
  RXResult res = RXSuccess;
  res.expression = original;
  
  expr_t expr = (expr_t) xmalloc(sizeof(symbol_t) * length);
  
  byte_t byte;
  int i;
  for(i = 0; (byte = original[i]); ++i) {
    expr[i] = byte;
  }
  expr[i] = 0;
  
  *expr_buf = expr;
  return res;
}

static RXResult rx_compileexpr_basic(char *original, size_t length, expr_t *expr_buf) {
  assert(length > 0);
  
  RXResult res = RXSuccess;
  res.expression = original;
  
  /* A single symbol in expr_t format is represented by one or more bytes in the
   * string representation so we can safely assume the length will be +length+ at most.
   */
  expr_t expr = (expr_t) xmalloc(sizeof(symbol_t) * length);
  
  byte_t byte;
  /* state variables */
  enum {st_initial, st_normal, st_escape} st_state = st_initial;
  expr_t st_expr = expr - 1; /* any write to *st_expr is preceded with ++st_expr */
  char *st_str = original;
  do {
    byte = *st_str;
    ++st_str;
    
    switch(st_state) {
      case st_initial:
      switch(byte) {
        case '*': case '?': case '+':
        res.msg = rx_newmsg(
          "Parse error at pos %d: quantifier must follow a literal character or character class.",
          st_str - original);
        res.err = RX_ERR_PARSE_ERROR;
        return res;
        case '\\':
        st_state = st_escape;
        break;
        default:
        ++st_expr;
        *st_expr = byte;
        st_state = st_normal;
      }
      break;
      
      case st_normal:
      switch(byte) {
        case '*': case '?': case '+':
        
        if(*st_expr & (SHIFT_FLAG_KSTAR | SHIFT_FLAG_QUESTION | SHIFT_FLAG_KCROSS)) {
          res.err = RX_ERR_PARSE_ERROR;
          res.msg = rx_newmsg(
            "Parse error at pos %d: the previous symbol (%u) has already been quantified.",
              st_str - original, *st_expr & 0xffff);
          return res;
        }
        
        switch(byte) {
          case '*':
          *st_expr |= SHIFT_FLAG_KSTAR;
          break;
          case '?':
          *st_expr |= SHIFT_FLAG_QUESTION;
          break;
          case '+':
          *st_expr |= SHIFT_FLAG_KCROSS;
          break;
        }
        
        break;
        case '.':
        ++st_expr;
        *st_expr = SHIFT_CLASS_ANY;
        break;
        case '\\':
        st_state = st_escape;
        break;
        default:
        ++st_expr;
        *st_expr = byte;
      }
      break;
      
      case st_escape:
      switch(byte) {
        case 'd':
        ++st_expr;
        *st_expr = SHIFT_CLASS_DIGIT;
        st_state = st_normal;
        break;
        case 'l':
        ++st_expr;
        *st_expr = SHIFT_CLASS_LETTER;
        st_state = st_normal;
        break;
        case 's':
        ++st_expr;
        *st_expr = SHIFT_CLASS_SPACE;
        st_state = st_normal;
        default:
        ++st_expr;
        *st_expr = byte;
        st_state = st_normal;
      }
      break;
      
    }
    
  } while(byte != '\0');
  
  *expr_buf = expr;
  return res;
}

static RXResult rx_compileexpr(const char *bytes, size_t length, RXFormat format, RXSearchTermRef *term_buf) {
  RXResult res;
  size_t expr_len = length + 1; /* +1 tail for \0 */
  
  char *original = (char *) xmalloc(expr_len);
  strncpy(original, bytes, expr_len);
  
  expr_t expr;
  switch(format) {
    case RXFormatLiteral:
    res = rx_compileexpr_lit(original, expr_len, &expr);
    break;
    case RXFormatBasic:
    res = rx_compileexpr_basic(original, expr_len, &expr);
    break;
  }
  
  if(res.err == RX_ERR_SUCCESS) {
    RXSearchTermRef term = xmalloc(sizeof(struct RXSearchTerm));
    term->expr = expr;
    term->original = original;
    term->payload = 0;
    
    *term_buf = term;
  } else {
    *term_buf = 0;
  }
  
  return res;
}

static RXResult rx_ternary_insert(TreeRef node, expr_t expr, RXSearchTermRef term) {
  symbol_t symbol;
  TreeRef next;
  
  while((symbol = *expr)) {
    next = rx_maketree();
    node->links[2] = next;
    node = next;
    node->symbol = symbol;
    ++expr;
  }
  
  node->accepting_term = term;
  RXResult res = RXSuccess;
  res.expression = term->original;
  res.payload = term->payload;
  return res;
}

static RXResult rx_binary_insert(TreeRef node, expr_t expr, RXSearchTermRef term) {
  symbol_t symbol = *expr;
  TreeRef next = rx_maketree();
  node->links[(symbol > node->symbol)] = next;
  node = next;
  node->symbol = symbol;
  
  return rx_ternary_insert(node, expr + 1, term);
}

static RXResult rx_insert(RXSetRef set, RXSearchTermRef term) {
  RXResult res;
  
  /* walk to the point where the existing tree diverges from the
   * phrase being entered
   */
  expr_t expr = term->expr;
  symbol_t symbol;
  TreeRef node = set->root;
  TreeRef next;
  
  while((symbol = *expr)) {
    /* binary search to the last matching node */
    
    while(node->symbol != symbol) {
      next = node->links[(symbol > node->symbol)];
      if(next)
        node = next;
      else
        return rx_binary_insert(node, expr, term);
    }
    
    /* advance the ternary search */
    if(node->symbol != symbol || !node->links[2])
      return rx_ternary_insert(node, expr, term);
    
    node = node->links[2];
    ++expr;
  }
  
  /* If we land here we ran out of symbols, meaning the full phrase is
   * already in the set.
   */
  
  if(node->accepting_term) {
    /* The node is already accepting. This is a duplicate. */
    res.err = RX_ERR_DUPLICATE;
    res.expression = term->original;
    res.payload = term->payload;
    res.msg = 0;
    
    return res;
  } else {
    node->accepting_term = term;
    res = RXSuccess;
    res.expression = term->original;
    res.payload = term->payload;
    
    return res;
  }
}

void rx_freeterm(RXSearchTermRef term) {
  free(term->original);
  free(term->expr);
  free(term);
}

void rx_dumpsymbol(symbol_t symbol) {
  uint16_t base = symbol & 0xffff;
  byte_t literal = base & 0xff;
  byte_t printable = (literal <= 0x7f && literal >= 0x20) ? literal : 0;
  
  if(printable) {
    switch(printable) {
      case '"':
      printf("CHAR \\\"\\\"\\\" (0x%x)", printable);
      break;
      case '\\':
      printf("CHAR \\\"\\\\\\\" (0x%x)", printable);
      break;
      default:
      printf("CHAR \\\"%c\\\" (0x%x)", printable, printable);
    }
  } else if(literal) {
    printf("BYTE 0x%x", literal);
  } else {
    printf("SPECIAL 0x%x ", base);
    switch(base) {
      case SHIFT_CLASS_SPACE:
      printf("(\\s)");
      break;
      case SHIFT_CLASS_DIGIT:
      printf("(\\d)");
      break;
      case SHIFT_CLASS_LETTER:
      printf("(\\l)");
      break;
      case SHIFT_CLASS_ANY:
      printf("(.)");
      break;
    }
  }
  
  if(symbol & SHIFT_FLAG_KCROSS)
    printf(" +");
  if(symbol & SHIFT_FLAG_KSTAR)
    printf(" *");
  if(symbol & SHIFT_FLAG_QUESTION)
    printf(" ?");
}

static int rx_eachterm_helper(TreeRef node, int (*callback)(RXSearchTermRef)) {
  int i;
  TreeRef next;
  for(i = 0; i <= 2; ++i) {
    next = node->links[i];
    if(next) {
      if(!rx_eachterm_helper(next, callback))
        return 0;
    }
  }
  
  if(node->accepting_term)
    return callback(node->accepting_term);
  
  return 1;
}

static void rx_dumpnode(TreeRef node) {
  printf("%lu [label=\"", (uintptr_t) node);
  rx_dumpsymbol(node->symbol);
  printf("\"]\n");
  
  if(node->accepting_term) {
    printf("%lu [color=red]\n", (uintptr_t) node);
    //printf("%lu_term [shape=box color=red label=\"%s\"]\n", (uintptr_t) node, node->accepting_term->original);
    //printf("%lu_term -> %lu [style=dotted]\n", (uintptr_t) node, (uintptr_t) node);
  }
  
  int i;
  TreeRef next;
  for(i = 0; i <= 2; ++i) {
    next = node->links[i];
    if(next) {
      printf("%lu -> %lu", (uintptr_t) node, (uintptr_t) next);
      
      switch(i) {
        case 0: printf("[color=blue]\n"); break;
        case 1: printf("[color=green]\n"); break;
        case 2: printf("[color=red]\n"); break;
      }
      
      rx_dumpnode(next);
    }
  }
}

/* Public API */

RXSetRef rx_makeset(rx_freepayload_t freepayload) {
  RXSetRef _set = (RXSetRef) xmalloc(sizeof(struct RXSet));
  
  _set->count_expr = 0;
  _set->count_node = 0;
  _set->freepayload = freepayload;
  _set->root = rx_maketree();
  
  return _set;
}

void rx_compileset(RXSetRef set) {
  
}

void rx_freeset(RXSetRef set) {
  
}

void rx_freeresult(RXResult res) {
  if(res.msg)
    free(res.msg);
  
  /* These errors are returned when adding an expression and will result in its not
   * being added, meaning we should destroy the copy we made of it.
   */
  if(res.err <= RX_ERR_ADD_ERROR)
    free(res.expression);
}

RXResult rx_add(RXSetRef set, const char *bytes, size_t length, RXFormat format, void *payload) {
  RXSearchTermRef term;
  RXResult res = rx_compileexpr(bytes, length, format, &term);
  
  if(res.err != RX_ERR_SUCCESS)
    return res;
  
  term->payload = payload;
  
  res = rx_insert(set, term);
  
  if(res.err == RX_ERR_SUCCESS)
    set->count_expr++;
  
  return res;
}

RXResult rx_search(const RXSetRef set, const char *bytes, size_t length) {
  return RXSuccess; /* I'm on a horse. */
}

int rx_count(RXSetRef set) {
  return set->count_expr;
}

void rx_dumpall(RXSetRef set) {
  printf("digraph tree {\n");
  rx_dumpnode(set->root);
  printf("}\n");
}

void rx_dumpinfo(RXSetRef set) {
  
}

void rx_eachterm(RXSetRef set, int (*callback)(RXSearchTermRef)) {
  rx_eachterm_helper(set->root, callback);
}

void rx_dumpexpr(char *bytes, size_t length, RXFormat format) {
  RXSearchTermRef term;
  RXResult res = rx_compileexpr(bytes, length, format, &term);
  
  if(res.err != RX_ERR_SUCCESS) {
    fprintf(stderr, "rx_dumpexpr error %d - expression \"%s\" could not be compiled:\n%s\n", res.err, res.expression, res.msg);
    rx_freeresult(res);
    return;
  }
  
  rx_dumpterm(term);
  
  rx_freeresult(res);
  rx_freeterm(term);
}

int rx_dumpterm(RXSearchTermRef term) {
  expr_t expr;
  
  printf("digraph expression {\n");
  
  for(expr = term->expr; *expr != 0; ++expr) {
    printf("%lu [label=\"", (uintptr_t) expr);
    rx_dumpsymbol(*expr);
    printf("\"];\n");
    printf("%lu -> %lu;\n", (uintptr_t) expr, (uintptr_t) (expr + 1));
  }
  
  printf("%lu [label =\"\\0\"];\n", (uintptr_t) expr);
  printf("}\n");
  
  return 0;
}
