CC=gcc
CFLAGS=-c -Wall -ggdb
LDFLAGS=

# SRC_SEARCH=rxgrep.c rxset.c input.c
SRC_IPTOOL=ipscan.c input.c ip_tree.c

# EXE_SEARCH=rxgrep
EXE_IPTOOL=ipscan

# OBJ_SEARCH=$(SRC_SEARCH:.c=.o)
OBJ_IPTOOL=$(SRC_IPTOOL:.c=.o)

PERL = /usr/bin/env perl

# all: $(EXE_SEARCH) $(EXE_IPTOOL)
all: $(EXE_IPTOOL)

# $(EXE_SEARCH): $(OBJ_SEARCH) 
#		$(CC) $(LDFLAGS) $(OBJ_SEARCH) -o $@

$(EXE_IPTOOL): $(OBJ_IPTOOL)
	$(CC) $(LDFLAGS) $(OBJ_IPTOOL) -o $@

.cpp.o:
	$(CC) $(CFLAGS) $< -o $@

.PHONY: benchmark
benchmark:
	cd test && $(PERL) benchmark.pl "../$(PROGRAM)";

.PHONY: test
test:
	cd test && $(PERL) test.pl "../$(PROGRAM)";

clean:
	rm -rf ipscan rxgrep *.o *.dSYM
