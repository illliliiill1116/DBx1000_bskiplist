CC=g++
CFLAGS=-Wall -g -std=c++0x

.SUFFIXES: .o .cpp .h

SRC_DIRS = ./ ./benchmarks/ ./concurrency_control/ ./storage/ ./system/ ./storage/bskiplist/src/
INCLUDE = -I. -I./benchmarks -I./concurrency_control -I./storage -I./system -I./storage/bskiplist/include

CFLAGS += $(INCLUDE) -D NOGRAPHITE=1 -Werror -O3
LDFLAGS = -Wall -L. -L./libs -pthread -g -lrt -std=c++0x -O3 -ljemalloc -no-pie
LDFLAGS += $(CFLAGS)

CPPS = $(foreach dir, $(SRC_DIRS), $(wildcard $(dir)*.cpp))
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
