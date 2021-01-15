#ifndef CORE_STAT_HPP_
#define CORE_STAT_HPP_
#include <fstream>
#include <string>
#include <set>

using std::set;
using std::string;

#define NUM_CPU_STATES 10
#define STAT_FILE "/proc/stat"

class core_stat
{
public:
	int core_id;
	uint64_t event_count;
	// event_count / (load_count + store_count)
	double hw_threshold;
	double cpu_usage;
	set<int> batch_pids;
	set<int> lss_pids;

	core_stat();
	core_stat(int id);
	/*
	 * takes one line from /proc/stat and get time 
	 */
	void set_prev_time(string s);

	/* 
	 * takes one line from /proc/stat and get time 
	 */
	void set_time(string s);
	uint64_t get_interval();
	double get_cpu_usage();
	uint64_t get_event_count();
	void add_batch_pid(int pid);
	void remove_batch_pid(int pid);
	void clear_batch_pids();
	void add_lss_pid(int pid);
	void remove_lss_pid(int pid);
	void clear_lss_pids();

public:
	/* variables */
	uint64_t prev_running_time;
	uint64_t prev_idle_time;
	uint64_t running_time;
	uint64_t idle_time;

	/* methods */
	void set_cpu_usage();
};

#endif // CORE_STAT_HPP_
