CC=g++
CFLAGS=-Wall -g -std=c++0x

.SUFFIXES: .o .cpp .h

BPTREE_LEAFDS ?= 1
ifeq ($(BPTREE_LEAFDS),1)
BPTREE_TLX_DIR = ./storage/BP-tree/tlx-leafds
else
BPTREE_TLX_DIR = ./storage/BP-tree/tlx-plain
endif

SRC_DIRS = ./ ./benchmarks/ ./concurrency_control/ ./storage/ ./system/ ./storage/OLC_bskiplist/src/ ./storage/RW_bskiplist/src/
INCLUDE = -I. -I./benchmarks -I./concurrency_control -I./storage -I./system -I./storage/OLC_bskiplist/include \
		  -I./storage/RW_bskiplist/include -isystem./storage/RW_bskiplist/external/ParallelTools -isystem./storage/RW_bskiplist/external \
		  -isystem$(BPTREE_TLX_DIR) -isystem./storage/BP-tree/ParallelTools

CFLAGS += $(INCLUDE) -D NOGRAPHITE=1 -Werror -O3 -pthread #-fsanitize=thread 
LDFLAGS = -Wall -L. -L./libs -g -lrt -std=c++0x -O3 -ljemalloc -no-pie
LDFLAGS += $(CFLAGS)


BSKIP_CXX20_CFLAGS = $(filter-out -std=c++0x -Werror,$(CFLAGS)) -std=c++20 -DDEBUG=0 -DNDEBUG
storage/RW_bskiplist/src/%.o storage/RW_bskiplist/src/%.d: CFLAGS := $(BSKIP_CXX20_CFLAGS)
storage/index_rw_bskiplist.o storage/index_rw_bskiplist.d: CFLAGS := $(BSKIP_CXX20_CFLAGS)
storage/index_BPtree.o storage/index_BPtree.d: CFLAGS := $(BSKIP_CXX20_CFLAGS) -DBPTREE_LEAFDS=$(BPTREE_LEAFDS)

CPPS = $(foreach dir, $(SRC_DIRS), $(wildcard $(dir)*.cpp))

BSKIP_FRAGMENT_CPPS = ./storage/RW_bskiplist/src/InternalNode.cpp ./storage/RW_bskiplist/src/LeafNode.cpp ./storage/RW_bskiplist/src/range.cpp ./storage/RW_bskiplist/src/instances.cpp
CPPS := $(filter-out $(BSKIP_FRAGMENT_CPPS), $(CPPS))

CS = $(foreach dir, $(SRC_DIRS), $(wildcard $(dir)*.c))
OBJS = $(CPPS:.cpp=.o) $(CS:.c=.o)
DEPS = $(CPPS:.cpp=.d) $(CS:.c=.d)

all:rundb

rundb : $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

-include $(OBJS:%.o=%.d)

%.d: %.cpp
	$(CC) -MM -MT $*.o -MF $@ $(CFLAGS) $<

%.o: %.cpp
	$(CC) -c $(CFLAGS) -o $@ $<

%.d: %.c
	gcc -MM -MT $*.o -MF $@ $(INCLUDE) -D NOGRAPHITE=1 -Werror $<

%.o: %.c
	gcc -c -Wall -g -O3 $(INCLUDE) -D NOGRAPHITE=1 -Werror -o $@ $<

.PHONY: clean
clean:
	rm -f rundb $(OBJS) $(DEPS)
