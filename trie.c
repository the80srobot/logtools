#include "trie.h"
#include "common.h"
#include <inttypes.h>

/* private functions - forward declarations */
static void dumpnode(unsigned long id, unsigned idx);
static void dumptrie_walk(TrieNodeRef root);
static void dumpphrase(unsigned *sequence, int length, unsigned uniq);
static int compilephrase_rx(char *phrase, const char *end, unsigned **sequenceBuf, int *lengthBuf);
static int compilephrase(char *phrase, const char *end, unsigned **sequenceBuf, int *lengthBuf);
static inline TrieNodeRef makenode();

/* compile function table */
typedef int (*compilefn_t)(char *, const char*, unsigned**, int*);
static const compilefn_t compilers[2] = {&compilephrase, &compilephrase_rx};

/* Public interface - implementation */

TrieRef maketrie() {
  TrieRef _tr = (TrieRef) xmalloc(sizeof(struct Trie));
  _tr->phrase_count = 0;
  _tr->node_count = 0;
  _tr->root = makenode();
  
  return _tr;
}

void dumptrie(TrieRef trie) {
  puts("digraph trie {\n");
  
  /* print labels */
  printf("%lu [label=\"root\"];\n", (uintptr_t) trie->root);
  dumptrie_walk(trie->root);
  
  puts("}\n");
}

void dumpstats(TrieRef trie) {
  size_t sizetrie = sizeof(struct Trie);
  size_t sizenode = sizeof(struct TrieNode);
  size_t sizepayload = sizeof(struct TriePayload);
  size_t sizeall = sizetrie + ((sizenode + sizepayload) * trie->node_count);
  
  double size = sizeall;
  int unit = 0;
  while(size > 1024 && unit < 5) {
    ++unit;
    size /= 1024;
  }
  
  char units[5] = {0x20, 'k', 'M', 'G', 'T'};
  
  printf(
    "Trie:\n"
    "\tphrase count: %d\n"
    "\tnode count: %d\n"
    "\tsize per node: %zd bytes\n"
    "\tsize per node payload: %zd bytes\n"
    "\tbase size per trie: %zd bytes\n"
    "\ttotal size: %.2lf %cB (%zd bytes)\n",
    trie->phrase_count,
    trie->node_count,
    sizenode,
    sizepayload,
    sizetrie,
    size,
    units[unit],
    sizeall);
}

int addphrase(TrieRef trie, char *phrase, const char *end, TriePhrase format) {
  assert(phrase < end);
  
  /* parse the phrase */
  unsigned *sequence = 0;
  int length = 0;
  int res = 0;
  
  compilefn_t compiler = compilers[(int)format];
  
  if((res = compiler(phrase, end, &sequence, &length)) != 0)
    return res;
  
  /* walk to the point of divergence from the current state */
  TrieNodeRef node = trie->root;
  unsigned *s_pos = sequence;
  unsigned *s_end = sequence + length;
  unsigned letter;
  
  while(s_pos != s_end) {
    letter = *s_pos;
    ++s_pos;
    
    /* break if we're at the point of divergence */
    if(node->nodes[letter] == 0) {
      --s_pos;
      break;
    }
    
    node = node->nodes[letter];
  }
  
  while(s_pos != s_end) {
    letter = *s_pos;
    ++s_pos;
    
    node->nodes[letter] = makenode();
    ++trie->node_count;
    node = node->nodes[letter];
  }
  
  ++(trie->phrase_count);
  node->end_word = 1;
  free(sequence);
  
  return 0;
}

/* private functions - implementations */

static void dumpphrase(unsigned *sequence, int length, unsigned uniq) {
  printf("digraph phrase_%u {\n", uniq);
  
  int i;
  for(i = 0; i != length; ++i) {
    unsigned id = sequence[i];
    dumpnode(i, id);
    if(i + 1 != length)
      printf("%d -> %d;\n", i, i + 1);
  }
  
  printf("%u [color=blue];\n", length - 1);
  
  puts("}\n");
}

static int compilephrase_rx(char *phrase, const char *end, unsigned **sequenceBuf, int *lengthBuf) {
  /* Given that each byte in phrase represents at most one character in indices (with escape sequences
     representing fewer) we're save just making indices as long as the phrase */
  unsigned *sequence = (unsigned *) xmalloc(sizeof(int) * (end - phrase));
  int length = 0;
  int escape = 0;
  unsigned char byte;
  
  #define PUSH_IDX(IDX) sequence[length] = ((unsigned) IDX); ++length
  
  while(phrase != end) {
    byte = *phrase;
    ++phrase;
    
    if(escape) {
      escape = 0;
      switch(byte) {
        case 'S':
        PUSH_IDX(TRIE_WHITESPACE_GREEDY_IDX);
        break;
        case 's':
        PUSH_IDX(TRIE_WHITESPACE_IDX);
        break;
        case 'D':
        PUSH_IDX(TRIE_DIGIT_GREEDY_IDX);
        break;
        case 'd':
        PUSH_IDX(TRIE_DIGIT_IDX);
        break;
        default:
        PUSH_IDX(byte);
      }
    } else {
      switch(byte) {
        case '\\':
        escape = 1;
        break;
        case '*':
        PUSH_IDX(TRIE_KLEENE_IDX);
        break;
        case '?':
        PUSH_IDX(TRIE_WILDCARD_IDX);
        break;
        default:
        PUSH_IDX(byte);
      }
    }
  }
  
  *sequenceBuf = sequence;
  *lengthBuf = length;
  
  return 0;
}

static int compilephrase(char *phrase, const char *end, unsigned **sequenceBuf, int *lengthBuf) {\
  unsigned *sequence = (unsigned *) xmalloc(sizeof(unsigned) * (end - phrase));
  *lengthBuf = (int) (end - phrase);
  *sequenceBuf = sequence;
  
  for(; phrase != end; ++phrase, ++sequence) {
    *sequence = *phrase;
  }
  
  return 0;
}

static void dumpnode(unsigned long id, unsigned idx) {
  if(idx == '\\')
    printf("%lu [label=\"\\\\\"];\n", id);
  if(idx == '"')
    printf("%lu [label=\"\\\"\"];\n", id);
  else if(idx > 0x20 && 127 > idx)
    printf("%lu [label=\"%c\"];\n", id, idx);
  else if(idx == TRIE_KLEENE_IDX)
    printf("%lu [label=\"RX:* (kleene)\"];\n", id);
  else if(idx == TRIE_WILDCARD_IDX)
    printf("%lu [label=\"RX:? (wilcard)\"];\n", id);
  else if(idx == TRIE_WHITESPACE_GREEDY_IDX)
    printf("%lu [label=\"RX:\\\\S (whitespace, greedy)\"];\n", id);
  else if(idx == TRIE_WHITESPACE_IDX)
    printf("%lu [label=\"RX:\\\\s (whitespace)\"];\n", id);
  else if(idx == TRIE_DIGIT_GREEDY_IDX)
    printf("%lu [label=\"RX:\\\\S (digit, greedy)\"];\n", id);
  else if(idx == TRIE_DIGIT_IDX)
    printf("%lu [label=\"RX:\\\\s (digit)\"];\n", id);
  else
    printf("%lu [label=\"LITERAL: %d\"];\n", id, idx);
}

/*
 * This is written recursively, because I didn't feel like implementing a stack just to debug the trie.
 * These structures are fairly shallow (the max depth is equal to the length of the longest possible match),
 * so it's probably fine.
 */
static void dumptrie_walk(TrieNodeRef root) {
  TrieNodeRef *branch = root->nodes;
  TrieNodeRef *end = root->nodes + TRIE_BRANCHING + 1;
  TrieNodeRef node;
  int letter;
  
  for(; branch != end; ++branch) {
    node = *branch;
    if(node) {
      letter = (int) (branch - root->nodes);
      dumpnode((uintptr_t) node, letter);
      
      if(node->end_word)
        printf("%lu [color=blue];\n", (uintptr_t) node);
      
      printf("%lu -> %lu;\n", (uintptr_t) root, (uintptr_t) node);
      dumptrie_walk(*branch);
    }
    
  }
}

static inline TrieNodeRef makenode() {
  TrieNodeRef _tr = (TrieNodeRef) xmalloc(sizeof(struct TrieNode));
  _tr->end_word = 0;
  _tr->payload = 0;
  memset(_tr->nodes, 0, TRIE_BRANCHING * sizeof(TrieNodeRef));
  
  return _tr;
}
