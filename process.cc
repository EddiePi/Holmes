#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <map>

#include <stdio.h>
#include <sched.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>

#include "process.hpp"

using std::istringstream;
using std::ofstream;
using std::ifstream;
using std::cout;
using std::cerr;
using std::endl;
using std::map;
using std::pair;

proc_thread::proc_thread(int pid, int tid)
{
	this->pid = pid;
	this->tid = tid;
	this->cpu_usage = -1.0;
	this->should_migrate = false;
}


float proc_thread::get_cpu_usage() const
{
	return this->cpu_usage;
}

process::process(int pid, process_type type, conf *numa_conf)
{
	this->pid = pid;
	this->type = type;
	this->numa_conf = numa_conf;
	int cpu_num = numa_conf->get_int("cpu.num");
	for (int i = 0; i < cpu_num; i++)
	{
		this->core_thread_count.insert(pair<int, int>(i, 1));
	}
	this->full_cgroup_path = "";
	this->cpu_period = 100000;
	this->cpu_quota = -1;
	//	this->cpu_prev = 0;
	//	this->cpu_cur = 0;
	//	this->cpu_interval = -1;
	this->cpu_usage = 0.0;
	this->proc_path = "/proc/" + std::to_string(pid) + "/";
}

double process::get_cpu_limit() const
{
	if (cpu_quota <= 0)
	{
		return -1.0;
	}
	else
	{
		return (double)(cpu_quota * 1.0) / cpu_period;
	}
}

int process::set_cpu_limit(double limit) const
{
	string quota_file_path;
	quota_file_path = this->full_cgroup_path + "/cpu.cfs_quota_us";
	if (limit >= 0.0)
	{
		this->cpu_quota = (int)this->cpu_period * limit;
	}
	else
	{
		this->cpu_quota = -1;
	}
	ofstream quota_file(quota_file_path);
	if (!quota_file.is_open())
	{
		cerr << "write " << quota_file_path << " failed" << endl;
		return -1;
	}
	quota_file << this->cpu_quota;
	quota_file.close();
	return 0;
}

/* change affinity of all threads of this process */
int process::allocate_cores(set<int> &cores) const
{
	/* use command line to update the core set of this process */
	/* I did not use sched_setaffinity() since it must be applied on all
	 * threads and I am lazy
	 */
	FILE *fp;
	char res[160];
	string command = "/usr/bin/taskset -apc ";
	for (auto it = cores.begin(); it != cores.end(); it++)
	{
		command += std::to_string(*it) + ",";
	}
	command = command.substr(0, command.length() - 1);
	command += " " + std::to_string(this->pid);
	fp = popen(command.c_str(), "r");
	if (fp == NULL)
	{
		cerr << "change cpu set of pid " << this->pid << " failed. errorno: " << strerror(errno) << endl;
		return -1;
	}
	int res_line_count;
	if (this->type == TYPE_LSS)
	{
		res_line_count = 0;
		while (fgets(res, sizeof(res), fp) != NULL)
		{
			istringstream iss(res);
			string tid_str;
			int tid;
			iss >> tid_str;
			iss >> tid_str;
			tid_str = tid_str.substr(0, tid_str.length() - 2);
			tid = std::stoi(tid_str);
			/* update thread_map */
			update_thread_affinity_info(tid, cores);
			res_line_count++;
		}
		res_line_count /= 2;
	}
	else
	{
		while (fgets(res, sizeof(res), fp) != NULL);
		res_line_count = 1;
	}
	pclose(fp);

	/* update core_set_count */
	this->core_thread_count.clear();
	for (auto core_id: cores)
	{
		core_thread_count.insert(pair<int, int>(core_id, res_line_count));
	}
	return 0;
}

int process::enforce_affinity() const
{
	//cout << "enforcing affinity for pid: " << this->pid << endl;
	FILE *fp;
	char res[160];
	string command = "/usr/bin/taskset -apc ";
	for (auto it = this->core_thread_count.begin(); it != core_thread_count.end(); it++)
	{
		command += std::to_string(it->first) + ",";
	}
	command = command.substr(0, command.length() - 1);
	command += " " + std::to_string(this->pid);
	fp = popen(command.c_str(), "r");
	if (fp == NULL)
	{
		cerr << "change cpu set of pid " << this->pid << " failed. errno: " << strerror(errno) << endl;
		return -1;
	}
	while (fgets(res, sizeof(res), fp) != NULL);
	pclose(fp);
	return 0;
}

int process::remove_and_relocate_cores(const set<int> &cores_to_remove) const
{
	set<int> new_cores;
	for (auto it = core_thread_count.begin(); it != core_thread_count.end();)
	{
		auto cit = cores_to_remove.find(it->first);
		if (cit != cores_to_remove.end())
		{
			it = core_thread_count.erase(it);
		}
		else
		{
			new_cores.insert(it->first);
			it++;
		}
	}
	int res;
	res = allocate_cores(new_cores);
	return res;
}

int process::add_cores_and_relocate_cores(const set<int> &cores_to_add) const
{
	set<int> new_cores;
	for (auto core_id: cores_to_add)
	{
		new_cores.insert(core_id);
	}
	for (auto p: core_thread_count)
	{
		new_cores.insert(p.first);
	}
	int res;
	res = allocate_cores(new_cores);
	return res;
}

/*
 * update the core_set info in thread_map
 * create proc_thread if the thread does not exist
 */
void process::update_thread_affinity_info(int tid, set<int> &core_set) const
{
	//std::cout << "update thread affinity info" << endl;
	auto it = this->thread_map.find(tid);
	if (it == this->thread_map.end())
	{
		it = this->thread_map.insert(pair<int, proc_thread>(tid, proc_thread(this->pid, tid))).first;
	}
	it->second.core_set.clear();
	it->second.core_set.insert(core_set.begin(), core_set.end());
	//std::cout << "finish update thread affin info" << endl;
}

/*
 * change the thread affinity in the system
 * this method should update both core_thread_count and thread_map
 */
int process::set_thread_affinity(proc_thread &thread, set<int> &core_set) const
{
	if (this->type == TYPE_BATCH)
	{
		cerr << "warning: setting thread affinity for a batch job" << endl;
	}
	int ret = 0;
	cpu_set_t c_set;
	CPU_ZERO(&c_set);
	for (auto core_id: core_set)
	{
		CPU_SET(core_id, &c_set);
	}

	cout << "set thread affinity. tid: " << thread.tid << " core(s): ";
	for (int core_id: core_set)
	{
		cout << core_id << " ";
	}
	cout << endl;

	ret = sched_setaffinity(thread.tid, sizeof(cpu_set_t), &c_set);
	if (ret != 0)
	{
		cerr << "set affinity for thread " << thread.tid << " failed. error: " << strerror(errno) << endl;
	}
	else
	{
		/* adjust core_thread_count in this process */
		/* add new cores first for performance reason*/
		for (auto core_id: core_set)
		{
			auto core_thread_it = core_thread_count.find(core_id);
			if (core_thread_it != core_thread_count.end())
			{
				core_thread_it->second += 1;
			}
			else
			{
				core_thread_count.insert(pair<int, int>(core_id, 1));
			}
		}
		/* remove old cores */
		for (auto core_id: thread.core_set)
		{
			auto core_thread_it = core_thread_count.find(core_id);
			if (core_thread_it != core_thread_count.end())
			{
				if (core_thread_it->second <= 1)
				{
					core_thread_count.erase(core_thread_it);
				}
				else
				{
					core_thread_it->second -= 1;
				}
			}
		}
		update_thread_affinity_info(thread.tid, core_set);
	}
	return ret;
}

/*
int process::update_cpu_usage() const
{
	//std::cout << "getting cpu usage for pid: " << this->pid << endl;
	FILE *fp;
	char com_res[10];
	float usage = -1.0;
	string command = "ps -p " + std::to_string(pid) + " -o %cpu | sed -n '2p' | awk '{print $1}'";
	fp = popen(command.c_str(), "r");
	if (fp == NULL)
	{
		cerr << "fail to run command: " << command << endl;
		return -1;
	}
	while (fgets(com_res, sizeof(com_res), fp) != NULL)
	{
		if (usage < 0)
		{
			int iusage = atoi(com_res);
			usage = (float)iusage / 100;
		}
	}
	pclose(fp);
	this->cpu_usage = usage;
	return 0;
}
*/

/* @deprecated
int process::update_threads_cpu_usage(int tid) const
{
	map<int, float> thread_usage_map;
	FILE *fp;
	char com_res[80];
	string command = "ps -Lp " + std::to_string(this->pid) + " -o lwp,pcpu";
	fp = popen(command.c_str(), "r");
	if (fp == NULL)
	{
		cerr << "read thread cpu usage failed, pid: " << pid << endl;
		cerr << "errno no. " << strerror(errno) << endl;
		return -1;
	}
	// get the header line
	fgets(com_res, sizeof(com_res), fp);
	// get the pid line
	fgets(com_res, sizeof(com_res), fp);
	while (fgets(com_res, sizeof(com_res), fp) != NULL)
	{
		istringstream iss(com_res);
		int tid;
		float cpu_usage;
		iss >> tid;
		iss >> cpu_usage;
		cpu_usage /= 100.0;
		thread_usage_map.insert(std::pair<int, float>(tid, cpu_usage));
	}
	pclose(fp);

	// delete finished threads
	for (auto it = this->thread_map.begin(); it != this->thread_map.end();)
	{
		auto it1 = thread_usage_map.find(it->first);
		if (it1 == thread_usage_map.end())
		{
			it = thread_map.erase(it);
			continue;
		}
		it++;
	}

	// update cpu usage of all running threads
	for (auto it = thread_usage_map.begin(); it != thread_usage_map.end(); it++)
	{
		auto it1 = this->thread_map.find(it->first);
		if (it1 != this->thread_map.end())
		{
			// update thread cpu usage if it exists
			it1->second.cpu_usage = it->second;
		}
		else
		{
			// create a new proc_thread obj otherwise
			proc_thread new_thread(this->pid, it->first);
			new_thread.cpu_usage = it->second;
			auto main_thread_it = thread_map.find(this->pid);
			if (main_thread_it == thread_map.end())
			{
				cerr << "main thread is not recorded in the thread_map" << endl;
				return -1;
			}
			new_thread.core_set.insert(main_thread_it->second.core_set.begin(), main_thread_it->second.core_set.end());
			this->thread_map.insert(std::pair<int, proc_thread>(it->first, new_thread));
		}
	}
	return 0;
}
*/

void process::update_threads_cpu_time(bool is_prev) const
{
	DIR* t_dir;
	struct dirent *ent;
	set<int> cur_thread;
	string task_path = this->proc_path + "task/";
	/* update current running thread */
	if ((t_dir = opendir(task_path.c_str())) != NULL)
	{
		while ((ent = readdir(t_dir)) != NULL)
		{
			if (ent->d_name[0] == '.')
			{
				continue;
			}
			int tid = atoi(ent->d_name);
			string thread_stat_path = task_path + ent->d_name + "/stat";
			uint64_t thread_time = read_stat_file(thread_stat_path);
			auto it = thread_map.find(tid);
			if (it == thread_map.end())
			{
				it = thread_map.insert(pair<int, proc_thread>(tid, proc_thread(pid, tid))).first;
			}
			if (is_prev)
			{
				it->second.prev_time = thread_time;
			}
			else
			{
				it->second.cur_time = thread_time;
				it->second.cpu_usage = (float)(it->second.cur_time - it->second.prev_time) / this->global_interval;
			}
			cur_thread.insert(tid);
		}
		closedir(t_dir);
	}
	/* delete finished thread */
	for (auto it = thread_map.begin(); it != thread_map.end();)
	{
		if (cur_thread.find(it->first) == cur_thread.end())
		{
			it = thread_map.erase(it);
			continue;
		}
		it++;
	}
}

uint64_t process::read_stat_file(string file_path) const
{
	ifstream p_stat_file;
	p_stat_file.open(file_path);
	if (!p_stat_file.is_open())
	{
		cerr << "error in open stat file of pid: " << this->pid << endl;
		if (this->type == TYPE_BATCH)
		{
			cerr << "cgroup : " << this->full_cgroup_path << endl;
		}
		return -1;
	}
	string proc_stat_line;
	getline(p_stat_file, proc_stat_line);
	uint64_t time;
	istringstream iss(proc_stat_line);
	string dummy;
	uint64_t utime;
	uint64_t stime;
	for (int i = 0; i < 13; i++)
	{
		iss >> dummy;
	}
	iss >> utime;
	iss >> stime;
	//std::cout << "utime " << utime << " stime " << stime << std::endl; 
	time = utime + stime;
	p_stat_file.close();
	return time;
}

void process::update_prev_time() const
{
	this->cpu_prev = read_stat_file(this->proc_path + "stat");
	if (this->type == TYPE_BATCH)
	{
		return;
	}
	update_threads_cpu_time(true);
}

void process::update_cur_time(uint64_t global_interval) const
{
	this->cpu_cur = read_stat_file(this->proc_path + "stat");
	this->global_interval = global_interval;
	uint64_t cpu_interval;
	if (this->cpu_cur >= this->cpu_prev)
	{
		cpu_interval = cpu_cur - cpu_prev;
		this->cpu_usage = (double)cpu_interval / global_interval;
	}
	if (this->type == TYPE_BATCH)
	{
		return;
	}
	/*
	else
	{
		cout << "cur int: " << cpu_interval << "g int: " << global_interval << endl;
	}
	*/
	update_threads_cpu_time(false);
}

/*
void process::set_cpu_usage(uint64_t global_cpu_interval)
{
	if (cpu_interval < 0)
	{
		return;
	}
	this->cpu_usage = (double)this->cpu_interval / global_cpu_interval;
}

uint64_t process::parse_proc_line(string line)
{
	istringstream iss(line);
	string dummy;
	uint64_t utime;
	uint64_t stime;
	for (int i = 0; i < 13; i++)
	{
		iss >> dummy;
	}
	iss >> utime;
	iss >> stime;
	std::cout << "utime " << utime << " stime " << stime << std::endl;

	return (utime + stime);
}
*/
