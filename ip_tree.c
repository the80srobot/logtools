#include "ip_tree.h"

/* The reasons why this is a separate struct and has a typedef in the header file
 * basically have to do with making it easier to reuse this code in ASIM.
 */
struct IPTree {
 IPNodeRef root;
};

/* 0 = left, 1 = right. Using indices instead of struct members is an easy way to avoid conditionals. */
struct IPNode {
  IPNodeRef children[2];
};

/* Sentinel values to represent either a subnet range where no IPs exist or one that is fully occupied. */
static struct IPNode _zero;
static struct IPNode _full;
#define ZERO (&_zero)
#define FULL (&_full)

/* Private declarations */

static void node_insert(IPNodeRef *node_p, ip_t ip, int bit, int end);
static int node_search(IPNodeRef node, ip_t ip, int bit);
static void freenode(IPNodeRef node);
static void dumpnode(IPNodeRef node, ip_t ip, int bit);
static inline void dumpip(ip_t ip, int cidr);
static int validateip(ip_t, int cidr);

/* Detects the first full IP address in the string with an optional /CIDR block and populates ipbuf and blockbuf.
 * If CIDR block is not provided then blockbuf value is set to 32 to indicate a single IP.
 * Caller must free returned buffers.
 */
static int detectip_str(char *data, const char *end, ip_t **ips, int **blocks);

/* Public API */

int addip_str(IPTreeRef tree, char *data, const char *end) {
  ip_t *ips;
  int *blocks;
  int count;
  
  if((count = detectip_str(data, end, &ips, &blocks)) == 0)
    return IP_NOT_FOUND;
  
  return addip(tree, ips[0], blocks[0]);
}

int findip_str(IPTreeRef tree, char *data, const char *end, int pos) {
  ip_t *ips;
  int *blocks;
  int count;
  int idx;
  int res = 0;
  
  if((count = detectip_str(data, end, &ips, &blocks)) == 0)
    return IP_NOT_FOUND;
  
  if(pos == 0) {
    for(idx = 0; idx < count; ++idx) {
      if((res = findip(tree, ips[idx])))
        return res;
    }
    
    return 0;
  } else {
    idx = (pos > 0 ? pos - 1: count - pos);
    if(idx >= count)
      return IP_POS_OUT_OF_BOUNDS;
    
    return findip(tree, ips[idx]);
  }
  
}

IPTreeRef makeiptree() {
  IPTreeRef _tree = (IPTreeRef) xmalloc(sizeof(struct IPTree));
  _tree->root = ZERO;
  
  return _tree;
}

int addip(IPTreeRef tree, ip_t ip, int block) {
  int res;
  if((res = validateip(ip, block)) != 0)
    return res;
  
  node_insert(&(tree->root), ip, 31, 31 - block);
  return 0;
}

int findip(IPTreeRef tree, ip_t ip) {
  return node_search(tree->root, ip, 31);
}

void dumptree(IPTreeRef tree) {
  dumpnode(tree->root, 0, 31);
}

int iptree_empty(IPTreeRef tree) {
  return (tree->root == ZERO);
}

/* Private implementations */

static void dumpnode(IPNodeRef node, ip_t ip, int bit) {
  if(node == ZERO)
    return;
  
  if(node == FULL) {
    dumpip(ip, 31 - bit);
    return;
  }
  
  dumpnode(node->children[0], ip, bit - 1);
  dumpnode(node->children[1], ip | (1 << bit), bit - 1);
}

static inline void dumpip(ip_t ip, int cidr) {
  printf("%u.%u.%u.%u/%d\n", ip >> 24, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff, cidr);
}

static int validateip(ip_t ip, int cidr) {
  if(ip > 0xffffffff)
    return IP_ERROR_ADDRESS_INVALID_BAD_IP;
  
  if((ip & 0xffffffff & (0xffffffff << (32 - cidr))) != ip)
    return IP_ERROR_ADDRESS_INVALID_BAD_CIDR;
  
  return 0;
}

/* Adds an address to a node.
 * node_p - the node that the address is being added to. May be replaced with a sentinel value (ZERO or FULL).
 * ip - the IP address being added
 * bit - indicates which bit of the address the node_p represents and (therefore) how far down the tree it is
 * end - the size of the block being added, -1 being a /32, 0 a /31 and so on.
 */
static void node_insert(IPNodeRef *node_p, ip_t ip, int bit, int end) {
  IPNodeRef node;
  node = *node_p;
  
  /* Are we adding something that doesn't exist yet? If not then bail. */
  if(node == FULL)
    return;
  
  /* Are we at the point where we can operate? If so then set to FULL and bail. */
  if(bit <= end) {
    if(node != ZERO) freenode(node);
    *node_p = FULL;
    return;
  }
  
  /* If we ended up in a fresh branch then create a new node here. */
  if(node == ZERO) {
    node = xmalloc(sizeof(struct IPNode));
    node->children[0] = ZERO;
    node->children[1] = ZERO;
    *node_p = node;
  }
  
  /* Recur using either the left or the right branch based on the bit value. */
  node_insert(&node->children[(ip >> bit) & 1], ip, bit -1, end);
  
  /* If both branches are full then collapse them. */
  if((node->children[0] == FULL) && (node->children[1] == FULL)) {
    free(node);
    *node_p = FULL;
  }
}

static int node_search(IPNodeRef node, ip_t ip, int bit) {
  if(node == FULL)
    return 1;
  
  if(node == ZERO)
    return 0;
  
  return node_search(node->children[(ip >> bit) & 1], ip, bit -1);
}

static void freenode(IPNodeRef node) {
  if((node == FULL || node == ZERO))
    return;
  
  freenode(node->children[0]);
  freenode(node->children[1]);
  free(node);
}

/* By default, detectip_str will use a statically-allocated buffer to store the
 * IPs it detects. If it ever runs out of space it'll allocate a dynamic buffer
 * and use that instead.
 *
 * In hindsight it would have made more sense to just use a dynamic buffer, but I was
 * tired and I had a deadline to meet. I'll probably scrap this whole thing later and 
 * replace it with Ragel, at which point I'll make it thread-safe.
 */
#define IPS_PER_LINE 4
static unsigned long detectip_max = IPS_PER_LINE;
static ip_t detectip_ips_static[IPS_PER_LINE];
static int detectip_blocks_static[IPS_PER_LINE];
static ip_t *detectip_ips = detectip_ips_static;
static int *detectip_blocks = detectip_blocks_static;

static int detectip_str(char *data, const char *end, ip_t **ipsbuf, int **blocksbuf) {
  /* This is basically just a state machine. */
  int count = 0;
  
  unsigned char byte;
  ip_t ip;
  unsigned char ipbyte;
  int block;
  
  #define STATE_LOOP_BEGIN_EOL_FINISH while(1) { if(data > end) {goto finish;}; byte = *data; ++data; 
  #define STATE_LOOP_BEGIN_EOL_TAIL while(1) { if(data > end) {goto tail;}; byte = *data; ++data;
  #define STATE_LOOP_END }
  
  init:
  ip = 0;
  ipbyte = 0;
  block = 32;
  goto scan;
  
  scan:
  STATE_LOOP_BEGIN_EOL_FINISH
    switch(byte) {
      case '0'...'9':
      ip = 0;
      ipbyte = byte - '0';
      goto ipbyte1;
    }
  STATE_LOOP_END
  
  ipbyte1:
  STATE_LOOP_BEGIN_EOL_FINISH
    switch(byte) {
      case '0'...'9':
      ipbyte *= 10;
      ipbyte += byte - '0';
      break;
      case '.':
      goto dot1;
      default:
      goto scan;
    }
  STATE_LOOP_END
  
  dot1:
  STATE_LOOP_BEGIN_EOL_FINISH
    switch(byte) {
      case '0'...'9':
      ip = ipbyte;
      ipbyte = byte - '0';
      goto ipbyte2;
      default:
      goto scan;
    }
  STATE_LOOP_END
  
  ipbyte2:
  STATE_LOOP_BEGIN_EOL_FINISH
    switch(byte) {
      case '0'...'9':
      ipbyte *= 10;
      ipbyte += byte - '0';
      break;
      case '.':
      goto dot2;
      default:
      goto scan;
    }
  STATE_LOOP_END
  
  dot2:
  STATE_LOOP_BEGIN_EOL_FINISH
    switch(byte) {
      case '0'...'9':
      ip *= 0x100;
      ip += ipbyte;
      ipbyte = byte - '0';
      goto ipbyte3;
      default:
      goto scan;
    }
  STATE_LOOP_END
  
  ipbyte3:
  STATE_LOOP_BEGIN_EOL_FINISH
    switch(byte) {
      case '0'...'9':
      ipbyte *= 10;
      ipbyte += byte - '0';
      break;
      case '.':
      goto dot3;
      default:
      goto scan;
    }
  STATE_LOOP_END
  
  dot3:
  STATE_LOOP_BEGIN_EOL_FINISH
    switch(byte) {
      case '0'...'9':
      ip *= 0x100;
      ip += ipbyte;
      ipbyte = byte - '0';
      goto ipbyte4;
      default:
      goto scan;
    }
  STATE_LOOP_END
  
  ipbyte4:
  STATE_LOOP_BEGIN_EOL_TAIL
    switch(byte) {
      case '0'...'9':
      ipbyte *= 10;
      ipbyte += byte - '0';
      break;
      case '/':
      ip *= 0x100;
      ip += ipbyte;
      ipbyte = 0;
      goto cidr1;
      default:
      ip *= 0x100;
      ip += ipbyte;
      ipbyte = 0;
      goto found;
    }
  STATE_LOOP_END
  
  cidr1:
  STATE_LOOP_BEGIN_EOL_TAIL
    switch(byte) {
      case '0'...'9':
      block = byte - '0';
      goto cidr2;
      default:
      block = 32;
      goto found;
    }
  STATE_LOOP_END
  
  cidr2:
  STATE_LOOP_BEGIN_EOL_TAIL
    switch(byte) {
      case '0'...'9':
      block *= 10;
      block += byte - '0';
      goto found;
      default:
      goto found;
    }
  STATE_LOOP_END
  
  tail:
  if(ipbyte) {
    ip *= 0x100;
    ip += ipbyte;
  }
  goto found;
  
  found:
  if(count > detectip_max) {
    /* If we ever run out of space in the static buffer we allocate a chunk of dynamic memory */
    if(detectip_max == IPS_PER_LINE) {
      detectip_max *= 2;
      ip_t *ips = (ip_t *) xmalloc(sizeof(ip_t) * detectip_max);
      int *blocks = (int *) xmalloc(sizeof(int) * detectip_max);
      memcpy(ips, detectip_ips, IPS_PER_LINE * sizeof(ip_t));
      memcpy(blocks, detectip_blocks, IPS_PER_LINE * sizeof(int));
      detectip_ips = ips;
      detectip_blocks = blocks;
    } else {
      detectip_max *= 2;
      detectip_ips = (ip_t *) xrealloc(detectip_ips, sizeof(ip_t) * detectip_max);
      detectip_blocks = (int *) xrealloc(detectip_blocks, sizeof(int) * detectip_max);
    }
    
  }
  
  detectip_ips[count] = ip;
  detectip_blocks[count] = block;
  ++count;
  
  goto init;
  
  finish:
  
  *ipsbuf = detectip_ips;
  *blocksbuf = detectip_blocks;
  
  return count;
}
