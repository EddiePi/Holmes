CC = g++

OBJS = check_cpu.o
OBJS1 = check_mem.o
CFLAGS = -std=c++14
LFLAGS = -lpthread

start : $(OBJS) $(OBJS1)
	$(CC) $(CFLAGS) ../core_stat.o -o check_cpu $(OBJS) $(LFLAGS)
	$(CC) $(CFLAGS) -o check_mem $(OBJS1) $(LFLAGS)

check_cpu.o  : check_cpu.cc ../core_stat.hpp
	$(CC) $(CFLAGS) -c check_cpu.cc $(LFLAGS)

check_mem.o  : check_mem.cc
	$(CC) $(CFLAGS) -c check_mem.cc $(LFLAGS)