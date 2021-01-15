CC = g++

OBJS = main.o pebs_event.o utils.o core_stat.o conf.o cg_manage.o process.o socket_stat.o
CFLAGS = -std=c++14
LFLAGS = -lpthread

OBJS1 = check_latency.o pebs_event.o utils.o


start  : $(OBJS) $(OBJS1)
	$(CC) $(CFLAGS) -o main $(OBJS) $(LFLAGS)
	$(CC) $(CFLAGS) -o check_latency $(OBJS1) $(LFLAGS)
main.o  : main.cc pebs_event.hpp utils.hpp core_stat.hpp conf.hpp cg_manage.hpp process.hpp socket_stat.hpp
	$(CC) $(CFLAGS) -c main.cc $(LFLAGS)
pebs_event.o  : pebs_event.cc
	$(CC) -c pebs_event.cc
check_latency.o : check_latency.cc utils.hpp
	$(CC) $(CFLAGS) -c check_latency.cc $(LFLAGS)
utils.o : utils.cc utils.hpp
	$(CC) -c utils.cc
core_stat.o : core_stat.cc core_stat.hpp
	$(CC) -c core_stat.cc
conf.o : conf.cc conf.hpp
	$(CC) -c conf.cc
cg_manage.o : cg_manage.cc cg_manage.hpp conf.hpp
	$(CC) -c cg_manage.cc
process.o : process.cc process.hpp conf.hpp
	$(CC) -c process.cc
socket_stat.o : socket_stat.cc socket_stat.hpp
	$(CC) -c socket_stat.cc

clean :
	rm main $(OBJS)
