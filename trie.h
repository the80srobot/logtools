#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#ifndef TRIE
#define TRIE

typedef struct TrieNode *TrieNodeRef;
typedef struct TriePayload *TriePayloadRef;
typedef struct Trie *TrieRef;

struct Trie {
  TrieNodeRef     root;
  int             phrase_count;
  int             node_count;
};

#define TRIE_BYTES 0x100
#define TRIE_SPECIAL 6
#define TRIE_WILDCARD_IDX 0x101
#define TRIE_KLEENE_IDX 0x102
#define TRIE_WHITESPACE_GREEDY_IDX 0x103
#define TRIE_WHITESPACE_IDX 0x104
#define TRIE_DIGIT_GREEDY_IDX 0x105
#define TRIE_DIGIT_IDX 0x106
#define TRIE_BRANCHING (TRIE_BYTES + TRIE_SPECIAL)

typedef enum {
  TriePhraseLiteral = 0,
  TriePhraseRegex = 1
} TriePhrase;

struct TrieNode {
  int             end_word; /* if true then the path from root to this node represents a searchword */
  TrieNodeRef     nodes[TRIE_BRANCHING]; /* branching factor is the possible values of a byte */
  TriePayloadRef  payload;
};

/*
 * Contains information about the word that was just found, such as the full phrase and destination where
 * output should be written
 */
struct TriePayload {
  int       fdout; /* filedescriptor where the line will be written - defaults to STDOUT */
  char      *dst; /* path to the output file - NULL for STDOUT */
  char      *phrase; /* full search term */
};

/* 
 * Adds +phrase+ to the root node of +trie+. +phrase+ must point to the start of a C string and the +end+ must point to the last byte
 * The string does not need to be null-terminated. If +format+ is TriePhraseRegex then the phrase is parsed as regex; otherwise
 * it is interpreted as a literal string.
 *
 * Regular expressions are close to UNIX masks. Supported matches are * (equiv. to Perl .*?), ? (equiv. to Perl .).
 * Backslash is used to force the following char to be interpretted literally (so that \* will result in '*'
 * being added instead of the operator *).
 */
int addphrase(TrieRef trie, char *phrase, const char *end, TriePhrase format);

/*
 * Dumps the trie to STDOUT in a .dot graph format compatible with Graphviz.
 */
void dumptrie(TrieRef trie);
void dumpstats(TrieRef trie);

/*
 * Guess what this does.
 */
TrieRef maketrie();

#endif
