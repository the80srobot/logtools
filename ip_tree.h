/* Basic bitwise tree to store IP address blocks in.
 * When adding addresses (or address blocks) the tree is always collapsed
 * so that any subtrees where all addresses are logically included is replaced with a
 * sentinel value indicating ALL and, conversely, any subtree where no IPs exist
 * is replaced with a sentinel ZERO. This leads to good performance for large continuous blocks
 * of IP addresses which is the primary use case.
 *
 * NOTE: I have no idea what this code will do on a big-endian system. It might break.
 * NOTE: It is NOT safe to call these functions from multiple threads, even when
 * using separate instances of the data structure.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "common.h"
#include <assert.h>
#include <stdint.h>

#ifndef IP_TREE
#define IP_TREE

#define IP_ERROR_ADDRESS_INVALID -1000
#define IP_ERROR_ADDRESS_INVALID_BAD_CIDR -1001
#define IP_ERROR_ADDRESS_INVALID_BAD_IP -1002

#define IP_NOT_FOUND -1100
#define IP_POS_OUT_OF_BOUNDS -1101

typedef struct IPTree *IPTreeRef;
typedef struct IPNode *IPNodeRef;
typedef uint32_t ip_t;

IPTreeRef makeiptree();

/* Unless explictly stated otherwise, the expected IP notation is dotted decimal.*/

/* Find the first valid IP in the string and add it to the tree. Supports CIDR notation. */
int addip_str(IPTreeRef tree, char *string, const char *end);

/* Find the first valid IP in the string and check for its presence in the tree. Supports CIDR notation. */
int findip_str(IPTreeRef tree, char *string, const char *end, int pos);

/* Add an IP to the tree. Second arg is CIDR block; pass 32 for single IP. */
int addip(IPTreeRef tree, ip_t ip, int block);
 
/* Return 1 if ip exists in the tree; otherwise return 0. */
int findip(IPTreeRef tree, ip_t ip);

int iptree_empty(IPTreeRef);
void dumptree(IPTreeRef tree);

#endif
