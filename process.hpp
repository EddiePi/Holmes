#ifndef PROCESS_HPP_
#define PROCESS_HPP_
#include <string>
#include <set>

#include "conf.hpp"

using std::string;
using std::set;

enum process_type
{
	TYPE_BATCH,
	TYPE_LSS
};

class proc_thread
{
public:
	/* variables */
	int pid;
	int tid;
	mutable uint64_t prev_time;
	mutable uint64_t cur_time;
	mutable set<int> core_set;
	mutable float cpu_usage;
	mutable bool should_migrate;

	/* method */
	proc_thread(int pid, int tid);
	float get_cpu_usage() const;
	bool operator< (const proc_thread &other) const
	{
		if (this->tid == other.tid)
		{
			return false;
		}
		else
		{
			return this->tid > other.tid;
		}
	}

};

class process
{
public:
	int pid;
	string proc_path;
	mutable string full_cgroup_path;
	/* counts the number of a threads on a core */
	mutable map<int, int> core_thread_count;
	mutable map<int, proc_thread> thread_map;
	mutable int cpu_period;
	mutable int cpu_quota;

	mutable uint64_t cpu_prev;
	mutable uint64_t cpu_cur;
	mutable uint64_t global_interval;

	mutable float cpu_usage;
	process_type type;
	conf *numa_conf;

	process(int pid, process_type type, conf *numa_conf);
	bool operator< (const process &other) const
	{
		if (this->pid == other.pid)
		{
			return false;
		}
		else
		{
			return this->pid > other.pid;
		}
	}
	void update_prev_time() const;
	void update_cur_time(uint64_t global_interval) const;
	/* general for lss and batch */
	int allocate_cores(set<int>& cores) const;
	int update_cpu_usage() const;

	/* for batch */
	double get_cpu_limit() const;
	int enforce_affinity() const;
	int set_cpu_limit(double limit) const;
	int remove_and_relocate_cores(const set<int> &cores_to_remove) const;
	int add_cores_and_relocate_cores(const set<int> &cores_to_add) const;

	/* for lss */
	int set_thread_affinity(proc_thread &thread, set<int> &core_set) const;

private:
	void update_thread_affinity_info(int tid, set<int> &core_set) const;
	void update_threads_cpu_time(bool is_prev) const;
	uint64_t read_stat_file(string file_path) const;

};

#endif // PROCESS_HPP_
