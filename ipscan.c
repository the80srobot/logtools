#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include "common.h"
#include <string.h>
#include "input.h"
#include "ip_tree.h"
#include "list.h"

static aio_buffer *buffer;
static IPTreeRef iptree;

static int once_warning_outofbounds = 1;

static int verbose = 1; /* print some additional messages */
static ListRef files = 0; /* files to load */
static ListRef ips = 0; /* inline ips to parse and load */
int search_ippos = 0;
int search_invertmatch = 0;

typedef enum {
  DebugNone = 0,
  DebugTree = 1
} Debug;

static int debuglvl = (int) DebugNone;

static void loadlist(void *arg) {
  char *path = (char *)arg;
  int res = 0;
  
  if((res = aio_buffer_open(buffer, path)) != 0) {
    if(verbose)
      fprintf(stderr, "Warning: could not open file %s, error code: %d.\n", path, res);
    return;
  }
  
  while((res = aio_buffer_loadline(buffer)) == 0) {
    res = addip_str(iptree, buffer->linestart, buffer->linelimit);
    
    if(verbose) {
      switch(res) {
        case IP_ERROR_ADDRESS_INVALID_BAD_IP:
        fprintf(stderr, "Warning: The IP address on the line below is invalid.\n%.*s\n", (int) (buffer->linelimit - buffer->linestart), buffer->linestart);
        break;
        case IP_ERROR_ADDRESS_INVALID_BAD_CIDR:
        fprintf(stderr, "Warning: The CIDR block on the line below is invalid.\n%.*s\n", (int) (buffer->linelimit - buffer->linestart), buffer->linestart);
        break;
        case IP_NOT_FOUND:
        fprintf(stderr, "Warning: The line below does not contain an IP address.\n%.*s\n", (int) (buffer->linelimit - buffer->linestart), buffer->linestart);
        break;
      }
    }
  }
  
  if(res != AIO_ERROR_END_BUFFER) {
    fprintf(stderr, "IO Error code %d.\n", res);
    exit(res);
  } else {
    return;
  }
}

static void loadip(void *arg) {
  char *ip = (char *)arg;
  unsigned int len = strlen(ip);
  int res = addip_str(iptree, ip, (ip + len));
  
  if(verbose) {
    switch(res) {
      case IP_ERROR_ADDRESS_INVALID_BAD_IP:
      fprintf(stderr, "Warning: The IP %s is invalid.\n", ip);
      break;
      case IP_ERROR_ADDRESS_INVALID_BAD_CIDR:
      fprintf(stderr, "Warning: The CIDR block %s is invalid.\n", ip);
      break;
      case IP_NOT_FOUND:
      fprintf(stderr, "Warning: No IP found in \"%s\".\n", ip);
      break;
    }
  }
}

static int work(IPTreeRef tree, int fd) {
  int res = 0;
  
  if((res = aio_buffer_init(buffer, fd)) != 0)
    return res;
  
  while((res = aio_buffer_loadline(buffer)) == 0) {
    res = findip_str(tree, buffer->linestart, buffer->linelimit, search_ippos);
    
    switch(res) {
      case 1:
      if(!search_invertmatch)
        aio_buffer_writeline(buffer, STDOUT_FILENO);
      break;
      case IP_POS_OUT_OF_BOUNDS:
      if(verbose && once_warning_outofbounds) {
        fprintf(stderr,
          "Warning: IP position %d is out of bounds for at least some lines in the input stream.\n",
          search_ippos);
        once_warning_outofbounds = 0;
      }
      case 0:
      if(search_invertmatch)
        aio_buffer_writeline(buffer, STDOUT_FILENO);
    }
      
  }
  
  if(res != AIO_ERROR_END_BUFFER)
    fprintf(stderr, "IO Error code %d.\n", res);
  
  return res;
}

static void print_version() {
  printf(
    "ipscan %d.%d.%d\n\n",
    VERSION_MAJOR, VERSION_RELEASE, VERSION_MINOR
  );
  
  exit(0);
}

static void print_usage() {
  printf(
    "Usage: ipscan [OPTION]...\n"
    "Search for IP addresses or CIDR blocks in STDIN and print out matched lines.\n"
    "\nLoading IP lists:\n"
    "  -i, --ip-list FILE\t\tload newline-separated list of IP addresses (CIDR notation supported)\n"
    "  -I, --ip-search IP\t\tadd the IP to the list of IP addresses searched for (CIDR notation is supported)\n"
    "\nSearch options:\n"
    "  -v, --invert-match\t\tinstead of printing lines that match the IP list, print ones that don't\n"
    "  -p, --match-position IDX\tinstead of checking against the first IP on the line, check against the IDXth\n"
    "\t\t\t\tSupports negative IDX, counting from right instead from left.\n"
    "\t\t\t\t(-1 = last IP, 1 = first IP, 0 = any position; default: 0)\n"
    "\nOutput control:\n"
    "  --dump-ips\t\t\tinstead of running the search dump the computed CIDR blocks to STDOUT\n"
    "  --verbose\t\t\tprint additional messages to STDERR (default)\n"
    "  --quiet\t\t\tdon't print messages to STDERR\n"
    "\nMiscellaneous:\n"
    "  -V, --version\t\t\tprint version information and exit\n"
    "  -h, --help\t\t\tprint this message and exit\n"
    "\nExamples:\n"
    "# Find all communication where neither source nor destination are in a private range:\n"
    "> cat /var/syslog/* | ipscan -v -I 10.0.0.0/8 -I 192.168.0.0/16 -I 172.16.0.0/12 -p 0\n"
    "# Find all communication originating from China:\n"
    "> cat /var/syslog/* | ipscan -i chinese_ranges.txt -p 0\n\n"
    "# Simplify a list of IP ranges:\n"
    "> ipscan -I 10.0.0.0/24 -I 10.0.1.0/24 --dump-ips\n"
    "\t# outputs: 10.0.0.0/23\n"
    );
  exit(0);
}

static inline void getopts(int argc, char **argv) {
  int c;
  while(1) {
    static struct option long_options[] = {
      {"verbose",         no_argument,        &verbose,   1},
      {"quiet",           no_argument,        &verbose,   0},
      {"ip-list",         required_argument,  0,          'i'},
      {"ip-search",       required_argument,  0,          'I'},
      {"match-position",  required_argument,  0,          'p'},
      {"invert-match",    no_argument,        0,          'v'},
      {"version",         no_argument,        0,          'V'},
      {"help",            no_argument,        0,          'h'},
      {"dump-ips",        no_argument,        &debuglvl,  (int) DebugTree},
      {0,0,0,0}
    };
    
    int opt_index;
    c = getopt_long(argc, argv, "i:I:p:hvV", long_options, &opt_index);
    if(c == -1)
      break;
    
    switch(c) {
      case 0:
      break;
      case 'i':
      files = LIST_APPEND_CPY(files, optarg);
      break;
      case 'p':
      search_ippos = atoi(optarg);
      break;
      case 'v':
      search_invertmatch = 1;
      case 'V':
      print_version();
      break;
      case 'I':
      ips = LIST_APPEND_CPY(ips, optarg);
      break;
      case 'h':
      print_usage();
      break;
      default:
      print_usage();
    }
  }
}

int main(int argc, char **argv) {
  if(argc == 1)
    print_usage();
  /* Initialize the global instances of IP tree and buffer */
  iptree = makeiptree();
  buffer = aio_buffer_alloc();
  
  getopts(argc, argv);
  
  list_each(files, &loadlist);
  list_free(files); files = 0;
  list_each(ips, &loadip);
  list_free(ips); ips = 0;
  
  if(iptree_empty(iptree) && verbose)
    fprintf(stderr, "Warning: no IP blocks have been loaded.\n");
  
  switch(debuglvl) {
    case DebugTree:
    dumptree(iptree);
    exit(0);
  }
  
  work(iptree, STDIN_FILENO);
  
  return 0;
}
