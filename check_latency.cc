#include <unistd.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <sched.h>
#include <stdio.h>
#include <inttypes.h>
#include <iostream>
#include <fstream>
#include <thread>
#include <ctime>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <cstdlib>
#include <cmath>
#include "pebs_event.hpp"
#include "utils.hpp"

using std::cout;
using std::cerr;
using std::cin;
using std::ofstream;
using std::endl;
using std::thread;
using std::vector;
using std::mutex;
using std::condition_variable;

#define GET_ARR_LEN(array) {(sizeof(array)/sizeof(array[0]))}

#define M 1048576 //1M
//#define RAW_EVT_NUM 0x25302a3 // L3 miss cycles
#define RAW_EVT_NUM 0x06a3
#define PROF_INT 1000000 // in us
#define IDLE_INT 1000 // in us
int S = 600; // allocation size per thread
char ***chunks;
mutex uncompleted_mutex;
int uncompleted_count;

mutex thread_mtx;
condition_variable cv;
bool should_access;
bool accessing;

int sockets[2][32] = 
	{{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
		 32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47},
	{16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,
		48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63}};

int nodes[2] = {0, 1};

long* chunk_access_time;

int set_affinity(int tid, int* core_ids)
{
	cpu_set_t mask;
	CPU_ZERO(&mask);
	int len;
	len = GET_ARR_LEN(core_ids);
	for(int i = 0; i < len; i++)
	{
		CPU_SET(core_ids[i], &mask);
	}
	int ret = sched_setaffinity(0, sizeof(mask), &mask);
}

int set_affinity_one(int tid, int core_id)
{
	cpu_set_t mask;
	CPU_ZERO(&mask);
	CPU_SET(core_id, &mask);
	int ret = sched_setaffinity(0, sizeof(mask), &mask);
	
}

void allocate(int tid, char** p)
{
	for(int i = 0; i < S; i++)
	{
		p[i] = new char[M];
		for(int j = 0; j < M; j++)
		{
			p[i][j] = 'a';
		}
		usleep(1000);
	}
}

void deallocate(int tid, char** p)
{
	for(int i = 0; i < S; i++)
	{
		delete []p[i];
	}
}

void do_access(int tid, char** p)
{
	struct timeval start;
	struct timeval end;
	long count = 0;
	std::srand(std::time(NULL));
	//cout << "thread " << tid << " start access" << endl;
	gettimeofday(&start, NULL);
	while (should_access)
	{
		int start_index = std::rand() % S;
		for(int n = 0; n < M; n++)
		{
			p[start_index][n] = 'b';
		}
		count++;
	}
	gettimeofday(&end, NULL);
	long interval = (long)get_interval(start, end);
	long mean_interval = (long)((double)interval/count);
	//cout << "interval : " << interval << " count: " << count << 
	//	" mean: " << mean_interval << endl;

	uncompleted_mutex.lock();
	chunk_access_time[tid] = mean_interval;;
	uncompleted_mutex.unlock();
}

void _access_(int index)
{
	// set the affinity of this thread
	int tid = syscall(SYS_gettid);
	set_affinity_one(tid, sockets[0][index]);
	chunks[index] = new char*[S];
	allocate(index, chunks[index]);
	
	uncompleted_mutex.lock();
	uncompleted_count--;
	if (uncompleted_count == 0)
	{
		accessing = true;
		cv.notify_one();
	}
	uncompleted_mutex.unlock();
	
	do_access(index, chunks[index]);
	deallocate(index, chunks[index]);
}

void report(vector<int> efds, ofstream &outfile)
{
	uint64_t values[3];
	uint64_t event_mean;
	uint64_t event_stdev;
	long chunk_mean;
	long chunk_stdev;
	int core_id = 0;
	uint64_t sum = 0;
	vector<uint64_t> counts;
	for(auto efd: efds)
	{
		read(efd, values, sizeof(values));
		//cout << "core: " << core_id << " cycles: " << values[0] << endl;
		//printf("core: %d cycles: %'" PRIu64 "\n", core_id, values[0]);
		counts.push_back(values[0]);
		sum += values[0];
		core_id++;
	}
	event_mean = (uint64_t)((double)sum/core_id);
	sum = 0;
	for (auto c: counts)
	{
		sum += (c - event_mean) * (c - event_mean);
	}
	event_stdev = (uint64_t)sqrt((double)sum/core_id);

	long sum2 = 0;
	for (int i = 0; i < core_id; i++)
	{
		sum2 += chunk_access_time[i];
	}
	chunk_mean = (long)((double)sum2/core_id);
	sum2 = 0;
	for (int i = 0; i < core_id; i++)
	{
		sum2 += (chunk_access_time[i] - chunk_mean) * (chunk_access_time[i] - chunk_mean);
	}
	chunk_stdev = (long)sqrt((double)sum2/core_id);
	cout << "avg. cycles: " << event_mean << " stdev: " << event_stdev 
		<< " chunk mean(ms): " << chunk_mean << " chunk stdev: " << chunk_stdev << endl;
	outfile << event_mean << " " << event_stdev << " " << chunk_mean << " " << chunk_stdev << endl;
	//printf("avg. cycles: %'"PRIu64"\n", count);
}

int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		cout << "using default allocation size 600MB" << endl;
	}
	else
	{
		S = atoi(argv[1]);
	}
	int pid = getpid();
	int tid = syscall(SYS_gettid);
	set_affinity_one(tid, 63);
	ofstream outfile("/home/epi/numa-project/data/latency.txt");
	outfile << "even_mean event_stdev chunk_mean chunk_stdev\n";
	char dummy;
	//vector<int> thread_nums = {1, 2, 4, 8, 16, 32};
	//vector<int> thread_nums = {1,2,4,6,8,10,12,14,16,20,24,28,32};
	vector<int> thread_nums;
	for (int i = 1; i <= 32; i++)
	{
		thread_nums.push_back(i);
	}
	vector<thread> access_threads;
	struct perf_event_attr pe;
	vector<int> efds;
	cout << "pid: " << pid << endl;
	cout << "press any key to start" << endl;
	int round = 1;
	dummy = getchar();

	// we only need to initialize the event once
	perf_event_init(&pe, RAW_EVT_NUM);

	for (auto config: thread_nums)
	{
		accessing = false;
		should_access = true;
		chunks = new char**[config];
		chunk_access_time = new long[config];
		uncompleted_count = config;
		for (int i = 0; i < config; i++)
		{
			access_threads.push_back(thread(&_access_, i));
		}
		std::unique_lock<mutex> lock(thread_mtx);
		
		//cout << "main waits on threads" << endl;
		//cv.wait(lock, []{return accessing;});
		while(!accessing)
		{
			cv.wait(lock);
		}
		
		// profile the l3 miss cycles

		// initialize the event
		for(int i = 0; i < config; i++)
		{
			int efd = perf_event_open(&pe, -1, sockets[0][i], -1, 0);
			if (efd == -1)
			{
				cerr << "error opening leader " << strerror(errno) << endl;
				exit(-1);
			}
			efds.push_back(efd);
		}

		// enable the event
		for(auto efd: efds)
		{
			perf_event_reset(efd);
			perf_event_enable(efd);
		}
		usleep(PROF_INT);

		// disable the event
		for(auto efd: efds)
		{
			perf_event_disable(efd);
		}

		

		//terminate all threads
		should_access = false;
		for(auto& th: access_threads)
		{
			th.join();
		}

		// report this round
		cout << "report round: " << round << " core num: " << config <<  endl;
		report(efds, outfile);


		// clean up memory
		delete []chunks;
		for(auto efd: efds)
		{
			close(efd);
		}

		delete []chunk_access_time;
		access_threads.clear();
		efds.clear();
		round++;
		cout << endl;
	}
	return 0;
}
