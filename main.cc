#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <cstdlib>
#include <unistd.h>
#include <vector>
#include <set>
#include <mutex>
#include <thread>
#include <memory>

#include <error.h>
#include <dirent.h>
#include <signal.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>

#include "cg_manage.hpp"
#include "conf.hpp"
#include "core_stat.hpp"
#include "pebs_event.hpp"
#include "process.hpp"
#include "read_format.hpp"
#include "socket_stat.hpp"
#include "utils.hpp"

using std::cerr;
using std::cin;
using std::cout;
using std::endl;
using std::ifstream;
using std::istringstream;
using std::mutex;
using std::ofstream;
using std::pair;
using std::set;
using std::string;
using std::thread;
using std::vector;

//#define CORE_NUM 64
//#define SOCKET_NUM 2
// const int32_t CORE_PER_SOCKET = 32;
//#define RAW_EVENT_NUM 0x02a3
const int32_t RAW_EVENT_NUM = 0x06a3;
const int32_t LOAD_EVENT_NUM = 0x81d0;
const int32_t STORE_EVENT_NUM = 0x82d0;
const double LSS_ACTIVE_THRESHOLD = 0.5;
const double THREAD_ACTIVE_THRESHOLD = 0.3;
const double CORE_IDLE_THRESHOULD = 0.2;
const double CPU_THROTTLE_RATE = 0.5;
//threshold for alloc batch on non-lss socket
const double SOCKET_BATCH_ALLOC_USAGE_THRESHOLD = 0.8 * CORE_PER_SOCKET;
//threshold for alloc batch on lss socket
const double SOCKET_BATCH_ON_LSS_USAGE_THRESHOLD = 0.9 * CORE_PER_SOCKET;
conf *numa_conf;
uint64_t EVENT_SEC_THRESHOLD = (uint64_t)160000000000;
uint64_t EVENT_THRESHOLD = EVENT_SEC_THRESHOLD;
double HW_METRIC_THRSHOLD = 50.0;
int32_t PROF_INT = 50000;
int32_t IDLE_INT = 50000;
int32_t MANAGE_INT = 100000;
int32_t AFTER_THROTTLE_INT = 100000;
uint64_t RESTORE_THRESHOLD = 1000000;

bool profile_enabled = false;
bool manage_enabled = false;
bool use_bulk_limit = true;

int socket_layout[SOCKET_NUM][CORE_PER_SOCKET] =
	{{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
	  32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47},
	 {16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
	  48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63}};

int nodes[2] = {0, 1};

// TODO delete these section
//double core_usage[2][32];
//mutex core_usage_mtx;

//uint64_t event_count[2][32];
//mutex event_count_mtx;

/* for user specified lss pid */
set<int> incoming_pids;
mutex incoming_pids_mtx;

/* monitor cgroup */
string cgroup_root;
string cgroup_batch;
string cgroup_batch_root;
// axiliary set recording batch cg groups for performance reason
set<string> current_batch_groups;
set<process> batch_processes;
set<process> lss_processes;
mutex process_mtx;

core_stat global_core_stat;
core_stat *core_stats[CORE_NUM];
mutex core_stat_mtx;

//uint64_t socket_event_count[SOCKET_NUM];
//double socket_cpu_usage[SOCKET_NUM];
double batch_cpu_usage;
double lss_cpu_usage;
mutex global_metric_mtx;

/*
	lss_core_set is initialized to the 0-7 cores on socket 0.
	batch_core_set is initialized to the rest of the cores.
*/
set<int> lss_init_core_set;
mutex lss_init_mtx;
//set<int> batch_init_core_set;
socket_stat *socket_stats[SOCKET_NUM];
mutex socket_stat_mtx;

/*
 * the order of mutex locking
 * process_mtx ->
 * core_stat_mtx ->
 * socket_stat_mtx ->
 */

/* declare functions */
void report_profile();
void report_manage();
void report_cgroup();
int get_pid_from_tid(int tid);
float get_cpu_usage(int pid);
void update_ls_pids(set<int> &core_set);
int choose_batch_socket();
void update_core_process_mapping(const process &p);
void remove_core_process_mapping(const process &p);
void update_socket_process_mapping(const process &p);
void remove_socket_process_mapping(const process &p);

/* profile */
float get_cpu_usage(int pid)
{
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
	return usage;
}

double get_socket_cpu_usage(int socket_id)
{
	if (socket_id >= SOCKET_NUM)
	{
		cerr << "socket id: " << socket_id << " does not exist" << endl;
	}
	return socket_stats[socket_id]->socket_cpu_usage;
}

uint64_t get_socket_event_count(int socket_id)
{
	if (socket_id >= SOCKET_NUM)
	{
		cerr << "socket id: " << socket_id << " does not exist" << endl;
	}
	return socket_stats[socket_id]->socket_event_count;
}

/* get the cpu usage of all lss or batch processes */
double get_processes_cpu_usage(process_type type)
{
	double total_usage = 0.0;
	set<process> *to_count;
	if (type == TYPE_BATCH)
	{
		to_count = &batch_processes;
	}
	else if (type == TYPE_LSS)
	{
		to_count = &lss_processes;
	}
	for (auto it = to_count->begin(); it != to_count->end(); it++)
	{
		if (it->cpu_usage >= 0)
		{
			total_usage += it->cpu_usage;
		}
	}
	return total_usage;
}

/**
 * choose the socket for the next batch job container
 * TODO choose sockets not holding lss first
 * if this sockets are busy, then choose other sockets holding lss.
 * if the other sockets are busy too, fall back to choose the
 * socket not holding lss.
 */
int choose_batch_socket()
{
	int target_socket = -1;
	float load_ratio = -1.0;
	vector<int> lss_sockets;
	vector<int> busy_batch_sockets;
	for (int i = 0; i < SOCKET_NUM; i++)
	{
		// skip sockets for lss first, we will revisit them later if
		// no socket is available
		if (socket_stats[i]->lss_init_cores.size() > 0)
		{
			lss_sockets.push_back(i);
			continue;
		}
		cout << "socket: " << i << " usage: " << socket_stats[i]->socket_cpu_usage << endl;
		if (socket_stats[i]->socket_cpu_usage > SOCKET_BATCH_ALLOC_USAGE_THRESHOLD)
		{
			busy_batch_sockets.push_back(i);
			continue;
		}
		int core_num = socket_stats[i]->batch_init_cores.size();
		int batch_num = socket_stats[i]->batch_pids.size();
		if (core_num == 0)
		{
			continue;
		}
		float current_ratio = (float)batch_num / core_num;
		if (load_ratio < 0 || load_ratio > current_ratio)
		{
			load_ratio = current_ratio;
			target_socket = i;
		}
	}
	if (target_socket >= 0)
	{
		cout << "vacant batch socket available: " << target_socket << endl;
		goto socket_found;
	}
	// iterate through lss sockets and find whether they are vacant
	for (int i : lss_sockets)
	{
		socket_stat *stat_p = socket_stats[i];
		if (stat_p->socket_cpu_usage < SOCKET_BATCH_ON_LSS_USAGE_THRESHOLD &&
			stat_p->socket_event_count < EVENT_THRESHOLD)
		{
			cout << "sharing lss socket: " << i << " usage: " << stat_p->socket_cpu_usage << " e-count: " << stat_p->socket_event_count << endl;
			target_socket = i;
			goto socket_found;
		}
	}
	// if the lss sockets are also busy, we fall back to batch sockets,
	// find the least used batch socket
	load_ratio = -1;
	for (int i : busy_batch_sockets)
	{
		int core_num = socket_stats[i]->batch_init_cores.size();
		int batch_num = socket_stats[i]->batch_pids.size();
		if (core_num == 0)
		{
			continue;
		}
		float current_ratio = (float)batch_num / core_num;
		if (load_ratio < 0 || load_ratio > current_ratio)
		{
			load_ratio = current_ratio;
			target_socket = i;
		}
	}
	cout << "fall back to use busy batch socket: " << target_socket << endl;
socket_found:
	return target_socket;
}

string get_container_id(int pid)
{
	int len;
	string proc_path = "/proc/";
	string res;
	proc_path += std::to_string(pid) + "/cwd";
	char full_cont_path[256];
	memset(full_cont_path, 0, sizeof(full_cont_path));
	len = readlink(proc_path.c_str(), full_cont_path, sizeof(full_cont_path));
	if (len < 0)
	{
		cerr << "error in read link: " << proc_path << " errno: " << strerror(errno) << endl;
		return "";
	}
	int start_index = strlen(full_cont_path);
	for (int i = start_index - 1; i >= 0; i--)
	{
		if (full_cont_path[i] == '/')
		{
			start_index = i + 1;
			break;
		}
	}
	std::size_t found;
	res = string(full_cont_path + start_index);
	found = res.find("container");
	if (found != string::npos && found == 0)
	{
		return res;
	}
	return "";
}

int monitor_batch_cgroup()
{
	/* monitor batch cgroup info */
	DIR *dir;
	struct dirent *ptr;
	char base[1000];
	set<string> new_batch_groups;
	set<process> new_batch_processes;
	set<int> running_batches_pid;

	//cout << "updating batch jobs" << endl;
	/*
	if ((dir = opendir(cgroup_batch_root.c_str())) == NULL)
	{
		cerr << "error in cgroup batch root: " << cgroup_batch_root << endl;
		cerr << "error no. " << strerror(errno) << endl;
		return -1;
	}
	*/
	// update running container pid

	FILE *fp;
	char res[256];
	string command = "/home/epi/lib/jdk/bin/jps";
	fp = popen(command.c_str(), "r");
	if (fp == NULL)
	{
		cerr << "Failed to run command: " << command << " errno: " << strerror(errno) << endl;
	}
	while (fgets(res, sizeof(res), fp) != NULL)
	{
		istringstream iss(res);
		int jpid;
		int next_batch_socket;
		string jname;
		string container_name;
		iss >> jpid;
		iss >> jname;
		if (jname.find("CoarseGrainedExecutorBackend") == string::npos &&
			jname.find("ExecutorLauncher") == string::npos)
		{
			continue;
		}
		container_name = get_container_id(jpid);
		if (container_name.length() == 0)
		{
			continue;
		}
		string full_cgroup_path = cgroup_batch_root + container_name;
		if (current_batch_groups.find(full_cgroup_path) == current_batch_groups.end())
		{
			//cout << "new batch cgroup detected. pid: " << jpid << " path: " << full_cgroup_path << endl;
			// add group path to new_batch_groups
			new_batch_groups.insert(full_cgroup_path);
			// build the process object
			process new_process(jpid, TYPE_BATCH, numa_conf);
			new_process.full_cgroup_path = full_cgroup_path;

			/* update socket_stats */
			socket_stat_mtx.lock();
			next_batch_socket = choose_batch_socket();
			socket_stat *stat_p = socket_stats[next_batch_socket];
			stat_p->batch_pids.insert(jpid);

			socket_stat_mtx.unlock();

			/* allocate cores to the process */
			new_process.allocate_cores(stat_p->batch_init_cores);
			/* add the process object to new_batch_processes */
			new_batch_processes.insert(new_process);

			/* update core_stats */
			core_stat_mtx.lock();
			update_core_process_mapping(new_process);
			/*
			for (auto core_id: stat_p->batch_init_cores)
			{
				core_stats[core_id]->add_batch_pid(pid);
			}
			*/
			core_stat_mtx.unlock();
		}
	}
	pclose(fp);

	/*
	while ((ptr = readdir(dir)) != NULL)
	{
		string d_name (ptr->d_name);
		if (d_name == "." || d_name == "..")
		{
			continue;
		}
		else if (ptr->d_type == 4)
		{
			string full_cgroup_path = cgroup_batch_root + d_name;
			if (current_batch_groups.find(full_cgroup_path) == current_batch_groups.end())
			{
				// add group path to new_batch_groups
				new_batch_groups.insert(full_cgroup_path);
				// build the process object
				ifstream task_file(full_cgroup_path + "/tasks");
				int pid;
				int next_batch_socket;
				// we read the second value since the first value is not a
				// tid of the yarn container
				task_file >> pid;
				task_file >> pid;
				task_file.close();
				process new_process(pid, TYPE_BATCH, numa_conf);
				new_process.full_cgroup_path = full_cgroup_path;

				socket_stat_mtx.lock();
				next_batch_socket = choose_batch_socket();
				socket_stat* stat_p = socket_stats[next_batch_socket];
				stat_p->batch_pids.insert(pid);

				socket_stat_mtx.unlock();

				new_process.allocate_cores(stat_p->batch_init_cores);
				new_batch_processes.insert(new_process);

				core_stat_mtx.lock();
				update_core_process_mapping(new_process);
				for (auto core_id: stat_p->batch_init_cores)
				{
					core_stats[core_id]->add_batch_pid(pid);
				}
				core_stat_mtx.unlock();
			}
		}
	}
	closedir(dir);
	*/
	//cout << "all cgroup dirs are read" << endl;

	/* delete finished batch cgroups */
	/* update socket_stats and core_stats */
	process_mtx.lock();
	for (auto it = batch_processes.begin(); it != batch_processes.end();)
	{
		string full_cg_path = it->full_cgroup_path;
		DIR *cg_dir;
		if ((cg_dir = opendir(full_cg_path.c_str())) == NULL)
		{
			/* we need to delete the finished batch cgroup */
			auto cg_it = current_batch_groups.find(full_cg_path);
			int pid_to_remove;
			if (cg_it != current_batch_groups.end())
			{
				current_batch_groups.erase(cg_it);
			}
			core_stat_mtx.lock();
			remove_core_process_mapping(*it);
			core_stat_mtx.unlock();

			socket_stat_mtx.lock();
			remove_socket_process_mapping(*it);
			socket_stat_mtx.unlock();
			it = batch_processes.erase(it);
			closedir(cg_dir);
			continue;
		}
		else
		{
			// enforce affinity for running batches
			//it->enforce_affinity();
		}
		closedir(cg_dir);
		it++;
	}
	process_mtx.unlock();
	//cout << "adding new jobs" << endl;
	// add new batch groups
	if (new_batch_groups.size() > 0)
	{
		process_mtx.lock();
		current_batch_groups.insert(new_batch_groups.begin(), new_batch_groups.end());
		batch_processes.insert(new_batch_processes.begin(), new_batch_processes.end());
		process_mtx.unlock();
	}
	//cout << "update batch job finished" << endl;
	return 0;
}

/**
 * currently we only support one lss process
 * additional lss will be allocate on the same 0-7 cores
 * this function should adjust socket_stats and lss_init_core_set
 * TODO to support multiple lss process
 */
void adjust_lss_init_cores()
{
	// do nothing here
}

/**
 * update finished mapping
 * update new lss mapping
 * note that this function does not touch running lss mapping
 */
void update_ls_pids(set<int> &lss_init_core_set)
{
	//cout << "updating ls pids" << endl;
	/* remove finished processes */
	/* update socket_stats and core_stats */
	process_mtx.lock();
	for (set<process>::iterator it = lss_processes.begin(); it != lss_processes.end();)
	{
		if (kill(it->pid, 0) < 0)
		{
			if (errno != ESRCH)
			{
				cerr << "error in detecting pid: " << it->pid << "remove it instead. errno: " << errno << endl;
			}
			/* update the core_stats and socket_stats */
			core_stat_mtx.lock();
			remove_core_process_mapping(*it);
			core_stat_mtx.unlock();

			socket_stat_mtx.lock();
			remove_socket_process_mapping(*it);
			socket_stat_mtx.unlock();
			it = lss_processes.erase(it);
			continue;
		}
		it++;
		/*
		else
		{
			DIR *dir;
			struct dirent *ptr;
			char base[1000];
			string path_str = "/proc/" + std::to_string(pid) + "/task";
			if ((dir = opendir(path_str.c_str())) == NULL)
			{
				cerr << "process does not exist" << endl;
				it++;
				continue;
			}
			while ((ptr = readdir(dir)) != NULL)
			{
				if (strcmp(ptr->d_name, ".") != 0 && strcmp(ptr-> d_name, "..") != 0)
				{
					int child = atoi(ptr->d_name);
					if (ls_pids.find(child) == ls_pids.end())
					{
						new_child_pids.insert(child);
					}
				}
			}
			closedir(dir);
			it++;
		}
		*/
	}
	process_mtx.unlock();

	//add new incoming pids specified by users
	//update core_stats. note that we need to update socket_stats
	//for lss for now
	//cout << "add new lss pids" << endl;
	if (!incoming_pids.empty())
	{
		incoming_pids_mtx.lock();
		for (auto pid : incoming_pids)
		{
			if (kill(pid, 0) == 0)
			{
				process lss_process(pid, TYPE_LSS, numa_conf);
				adjust_lss_init_cores();
				/* allocate cores for the incoming lss */
				lss_init_mtx.lock();
				cout << "allocating new cores" << endl;
				lss_process.allocate_cores(lss_init_core_set);
				set<int> sockets;
				/* update core_stats, record corresponding cores */
				cout << "construct core process mapping" << endl;
				core_stat_mtx.lock();
				for (auto core_id : lss_init_core_set)
				{
					int socket_id;
					int socket_pos;
					core_stats[core_id]->lss_pids.insert(pid);
					core_id_to_socket(socket_layout, core_id, socket_id, socket_pos);
					sockets.insert(socket_id);
				}
				core_stat_mtx.unlock();
				/* update socket_stats */
				socket_stat_mtx.lock();
				for (int socket_id : sockets)
				{
					socket_stats[socket_id]->lss_pids.insert(pid);
				}
				socket_stat_mtx.unlock();

				process_mtx.lock();
				cout << "insert into lss_processes" << endl;
				lss_processes.insert(lss_process);
				process_mtx.unlock();

				lss_init_mtx.unlock();
			}
		}
		//cout << "clear incoming pids" << endl;
		incoming_pids.clear();
		incoming_pids_mtx.unlock();
	}
	//cout << "end update ls pids" << endl;
}

/**
 * this is a thread,
 * 1. profile evnet_count and core_usage
 * 2. monitor batch job cgroup info
 */
void _profile_()
{
	cout << "profile thread start" << endl;

	/* variables about hardware event */
	//uint64_t values[3];
	char buf[4096];
	struct read_format* rf = (struct read_format*) buf;
	uint64_t count;
	uint64_t load_count;
	uint64_t store_count;
	struct perf_event_attr pe;
	struct perf_event_attr load_pe;
	struct perf_event_attr store_pe;
	int efds[CORE_NUM];
	int evt_ids[CORE_NUM][3];

	/* variables about cpu usage */
	ifstream stat_file = ifstream();
	string stat_line;

	perf_event_init(&pe, RAW_EVENT_NUM);
	perf_event_init(&load_pe, LOAD_EVENT_NUM);
	perf_event_init(&store_pe, STORE_EVENT_NUM);
	for (int i = 0; i < CORE_NUM; i++)
	{
		efds[i] = perf_event_open(&pe, -1, i, -1, 0);
		ioctl(efds[i], PERF_EVENT_IOC_ID, &evt_ids[i][0]);
		if (efds[i] == -1)
		{
			cerr << "error opening leader " << strerror(errno) << endl;
			return;
		}

		// load inst event
		int load_efd = perf_event_open(&load_pe, -1, i, efds[i], 0);
		ioctl(load_efd, PERF_EVENT_IOC_ID, &evt_ids[i][1]);
		if (load_efd == -1)
		{
			cerr << "error opening load event " << strerror(errno) << endl;
			exit(-1);
		}

		// store inst event
		int store_efd = perf_event_open(&store_pe, -1, i, efds[i], 0);
		ioctl(store_efd, PERF_EVENT_IOC_ID, &evt_ids[i][2]);
		if (store_efd == -1)
		{
			cerr << "error opening store event " << strerror(errno) << endl;
			exit(-1);
		}
	}
	//for (int i = 0; i < 2; i++)
	while (profile_enabled)
	{
		//cout << "start a round of profile" << endl;
		update_ls_pids(lss_init_core_set);
		monitor_batch_cgroup();

		// get per core cpu stat -- prev
		//cout << "enable hw events" << endl;
		stat_file.open(STAT_FILE);
		stat_file.seekg(0, std::ios::beg);
		// this is the global cpu stat line
		std::getline(stat_file, stat_line);
		global_core_stat.set_prev_time(stat_line);
		for (int i = 0; i < CORE_NUM; i++)
		{
			std::getline(stat_file, stat_line);
			//cout << "stat line " << stat_line << endl;
			core_stats[i]->set_prev_time(stat_line);
		}
		//stat_file.clear();
		stat_file.close();

		// get per process cpu stat -- prev
		for (auto it = batch_processes.begin(); it != batch_processes.end(); it++)
		{
			it->update_prev_time();
		}

		for (auto it = lss_processes.begin(); it != lss_processes.end(); it++)
		{
			it->update_prev_time();
		}

		// enable hardware event
		for (int i = 0; i < CORE_NUM; i++)
		{
			perf_event_reset(efds[i]);
			perf_event_enable(efds[i]);
		}
		usleep(PROF_INT);

		// disable hardware event
		for (int i = 0; i < CORE_NUM; i++)
		{
			perf_event_disable(efds[i]);
		}

		/* get per core cpu usage */
		stat_file.open(STAT_FILE);
		stat_file.seekg(0, std::ios::beg);
		std::getline(stat_file, stat_line);
		global_core_stat.set_time(stat_line);
		for (int i = 0; i < CORE_NUM; i++)
		{
			std::getline(stat_file, stat_line);
			core_stats[i]->set_time(stat_line);
		}
		//stat_file.clear();
		stat_file.close();

		// record hardware event
		//cout << "recording events" << endl;
		for (int i = 0; i < CORE_NUM; i++)
		{
			read(efds[i], buf, sizeof(buf));
			//cout << "core: " << i << " count: " << count << endl;
			// count = (uint64_t)((double)values[0] * values[1] / values[2]);
			//cout << "v1: " << values[0] << " v2: " << values[1] << " v3: " << values[2] << endl;
			//count = 0;
			for (int j = 0; j < rf->nr; j++) 
			{
				if (rf->values[j].id == evt_ids[i][0])
				{
					count = rf->values[j].value;
				}
				else if (rf->values[j].id == evt_ids[i][1])
				{
					load_count = rf->values[j].value;
				}
				else if (rf->values[j].id == evt_ids[i][2])
				{
					store_count = rf->values[j].value;
				}
			}
			core_stats[i]->event_count = count;
			core_stats[i]->hw_threshold = (double)count / (load_count + store_count);
		}

		/* get per process cpu usage -- cur */
		//cout << "getting per process cpu usage" << endl;
		process_mtx.lock();
		for (set<process>::iterator it = batch_processes.begin(); it != batch_processes.end(); it++)
		{
			it->update_cur_time(global_core_stat.get_interval() / CORE_NUM);
			//it->update_cpu_usage();
		}

		//cout << "getting lss cpu usage" << endl;
		for (set<process>::iterator it = lss_processes.begin(); it != lss_processes.end(); it++)
		{
			/* update process and thread cpu usage for this process */
			it->update_cur_time(global_core_stat.get_interval() / CORE_NUM);
			//cout << "lss pid: " << it->pid << " cpu usage: " << it->cpu_usage << endl;
			/*
			it->update_threads_cpu_usage();
			it->update_cpu_usage();
			*/
		}
		process_mtx.unlock();

		// update socket cpu usage here for performance reason
		global_metric_mtx.lock();
		// record cpu usage
		//cout << "aggregating socket cpu usage" << endl;
		for (int socket_id = 0; socket_id < SOCKET_NUM; socket_id++)
		{
			double socket_usage = 0.0;
			uint64_t socket_count = 0;
			for (int socket_pos = 0; socket_pos < CORE_PER_SOCKET; socket_pos++)
			{
				socket_usage += core_stats[socket_layout[socket_id][socket_pos]]->cpu_usage;
				socket_count += core_stats[socket_layout[socket_id][socket_pos]]->event_count;
			}
			socket_stats[socket_id]->socket_cpu_usage = socket_usage;
			socket_stats[socket_id]->socket_event_count = socket_count;
		}
		global_metric_mtx.unlock();

		/* update metric for batch and lss process group */
		//cout << "aggregating process group cpu usage" << endl;
		process_mtx.lock();
		double proc_cpu_usage;
		proc_cpu_usage = 0.0;
		for (auto it = batch_processes.begin(); it != batch_processes.end(); it++)
		{
			proc_cpu_usage += it->cpu_usage;
		}
		batch_cpu_usage = proc_cpu_usage;
		proc_cpu_usage = 0.0;
		for (auto it = lss_processes.begin(); it != lss_processes.end(); it++)
		{
			proc_cpu_usage += it->cpu_usage;
		}
		lss_cpu_usage = proc_cpu_usage;
		process_mtx.unlock();

		/* get per process cpu stat and usage -- cur */
		/*
		for (set<process>::iterator it =  batch_processes.begin(); it != batch_processes.end();)
		{
			int pid = it->pid;
			string proc_stat_path = "/proc/" + std::to_string(pid) + "/stat";
			ifstream proc_stat_file(proc_stat_path);
			if (proc_stat_file.is_open())
			{
				string proc_stat_line;
				std::getline(proc_stat_file, proc_stat_line);
				proc_stat_file.close();
				process copy = *it;
				it = batch_processes.erase(it);
				copy.set_cur_time(proc_stat_line);
				copy.set_cpu_usage(
							(global_core_stat.running_time +
							global_core_stat.idle_time -
							global_core_stat.prev_running_time -
							global_core_stat.prev_idle_time) /
							CORE_NUM);
				batch_processes.insert(copy);
				continue;
			}
			it++;
		}
		*/

		// TEST
		//report_profile();
		//report_cgroup();

		//cout << "end a round of profile" << endl;
		usleep(IDLE_INT);
	}

	for (int i = 0; i < CORE_NUM; i++)
	{
		close(efds[i]);
	}
	stat_file.close();
	return;
}

/* update core_stats based on the cores in p */
void update_core_process_mapping(const process &p)
{
	//cout << "update core process mapping" << endl;
	for (int core_id = 0; core_id < CORE_NUM; core_id++)
	{
		set<int> *pid_set;
		if (p.type == TYPE_BATCH)
		{
			pid_set = &core_stats[core_id]->batch_pids;
		}
		else
		{
			pid_set = &core_stats[core_id]->lss_pids;
		}
		int pid = p.pid;
		set<int>::iterator pid_it;
		if (p.core_thread_count.find(core_id) == p.core_thread_count.end() &&
			((pid_it = pid_set->find(pid)) != pid_set->end()))
		{
			/* remove unused core-process mapping */
			pid_set->erase(pid_it);
		}
		else if (p.core_thread_count.find(core_id) != p.core_thread_count.end() &&
				 ((pid_it = pid_set->find(pid)) == pid_set->end()))
		{
			/* add new used core-process mapping */
			pid_set->insert(pid);
		}
	}
	//cout << "finished update core process mapping" << endl;
}

void update_socket_process_mapping(process const &p)
{
	//cout << "update socket process mapping" << endl;
	set<int> sockets;
	for (auto it = p.core_thread_count.begin(); it != p.core_thread_count.end(); it++)
	{
		int socket_id;
		int socket_pos;
		core_id_to_socket(socket_layout, it->first, socket_id, socket_pos);
		sockets.insert(socket_id);
	}
	for (int socket_id = 0; socket_id < SOCKET_NUM; socket_id++)
	{
		set<int> *pid_set;
		if (p.type == TYPE_BATCH)
		{
			pid_set = &socket_stats[socket_id]->batch_pids;
		}
		else
		{
			pid_set = &socket_stats[socket_id]->lss_pids;
		}
		int pid = p.pid;
		set<int>::iterator pid_it;
		if (sockets.find(socket_id) == sockets.end() &&
			((pid_it = pid_set->find(pid)) != pid_set->end()))
		{
			/* remove unused core-process mapping */
			pid_set->erase(pid_it);
		}
		else if (sockets.find(socket_id) != sockets.end() &&
				 ((pid_it = pid_set->find(pid)) == pid_set->end()))
		{
			/* add new used core-process mapping */
			pid_set->insert(pid);
		}
	}
	//cout << "finished update socket process mapping" << endl;
}

/**
 * remove pid from core_stats
 * this function is called when a process ends
 */
void remove_core_process_mapping(const process &p)
{
	//cout << "remove core process mapping for process: " << p.pid << endl;
	for (auto ct : p.core_thread_count)
	{
		core_stat *stat = core_stats[ct.first];
		set<int> *pid_set;
		if (p.type == TYPE_BATCH)
		{
			pid_set = &stat->batch_pids;
		}
		else
		{
			pid_set = &stat->lss_pids;
		}
		auto it = pid_set->find(p.pid);
		if (it != pid_set->end())
		{
			pid_set->erase(it);
		}
	}
	//cout << "finished remove core process mapping" << endl;
}

void remove_socket_process_mapping(const process &p)
{
	//cout << "remove socket process mapping" << endl;
	for (int i = 0; i < SOCKET_NUM; i++)
	{
		set<int> *pid_set;
		if (p.type == TYPE_BATCH)
		{
			pid_set = &socket_stats[i]->batch_pids;
		}
		else
		{
			pid_set = &socket_stats[i]->lss_pids;
		}
		auto sit = pid_set->find(p.pid);
		if (sit != pid_set->end())
		{
			pid_set->erase(sit);
		}
	}
}

/* management */

int get_pid_from_tid(int tid)
{
	FILE *fp;
	char com_res[10];
	int pid = -1;
	string command = "ps -eLo pid= -o tid= | awk '$2 == " + std::to_string(tid) + "{print $1}'";
	fp = popen(command.c_str(), "r");
	if (fp == NULL)
	{
		cerr << "fail to run command: " << command << endl;
		return -1;
	}
	while (fgets(com_res, sizeof(com_res), fp) != NULL)
	{
		if (pid == -1)
		{
			pid = atoi(com_res);
		}
	}
	pclose(fp);
	return pid;
}

/**
 * find possible lss to adjust
 * we need to further check their thread usage
 * process_mtx is acquired in this function
 */
set<process> find_lss_to_adjust(int core_id, set<process> &lss_processes_to_adjust, int &thread_to_adjust_count)
{
	//cout << "finding lss thread to adjust" << endl;
	set<int> lss_pids_to_adjust;
	/* find all possible lss pid that might be adjusted on the busy core */
	for (int socket_pos = 0; socket_pos < CORE_PER_SOCKET; socket_pos++)
	{
		set<int> core_lss_set = core_stats[core_id]->lss_pids;
		if (core_lss_set.size() > 0)
		{
			lss_pids_to_adjust.insert(core_lss_set.begin(), core_lss_set.end());
		}
	}
	//cout << "find threads in possible process" << endl;

	// after this for loop, we only find the processes that runs on the core
	// we need to further find out whether their active thread is running on the socket too

	// find if the lss processes are active by check their cpu usgae and thread usage
	// and put them in an update set

	process_mtx.lock();
	for (auto it = lss_processes.begin(); it != lss_processes.end();)
	{
		bool should_migrate_process = false;
		if (lss_pids_to_adjust.find(it->pid) != lss_pids_to_adjust.end())
		{
			/* iterate through thread_map to find active threads */
			for (auto tit = it->thread_map.begin(); tit != it->thread_map.end(); tit++)
			{
				bool should_migrate_thread = false;
				//cout << "checking thread: " << tit->second.tid <<
				//	" cpu usage: " << tit->second.cpu_usage << endl;
				if (tit->second.cpu_usage > THREAD_ACTIVE_THRESHOLD)
				{
					set<int> &thread_cores = tit->second.core_set;
					if (thread_cores.find(core_id) != thread_cores.end())
					{
						should_migrate_thread = true;
						tit->second.should_migrate = true;
						thread_to_adjust_count++;
					}
				}
				/* mark the thread as should_migrate */
				should_migrate_process |= should_migrate_thread;
				//cout << "thread: " << tit->second.tid << " found" <<
				//	" migrate? " << tit->second.should_migrate << endl;
			}
			if (should_migrate_process)
			{
				//cout << "process " << it->pid << " should be migrated" << endl;
				lss_processes_to_adjust.insert(*it);
				it = lss_processes.erase(it);
				continue;
			}
		} // end if(lss_pids_to_adjust.find(it->pid) != lss_pids_to_adjust.end())
		it++;
	}
	process_mtx.unlock();
	//cout << "done finding lss to adjust" << endl;
	return lss_processes_to_adjust;
}

/**
 * remove the core from batch_init_core_set and
 * remove all the running batch jobs on the socket from this core
 */
void remove_batch_cores(const set<int> &cores_to_remove)
{
	/* adjust mapping in batch_processes */
	process_mtx.lock();
	for (auto it = batch_processes.begin(); it != batch_processes.end(); it++)
	{
		it->remove_and_relocate_cores(cores_to_remove);
	}
	process_mtx.unlock();

	// adjust mapping in core_stats
	// adjust mapping in socket_stats
	core_stat_mtx.lock();
	socket_stat_mtx.lock();
	int socket_id;
	int socket_pos;
	for (auto core_id : cores_to_remove)
	{
		core_stats[core_id]->clear_batch_pids();
		core_id_to_socket(socket_layout, core_id, socket_id, socket_pos);
		socket_stats[socket_id]->remove_batch_init_core(core_id);
	}
	socket_stat_mtx.unlock();
	core_stat_mtx.unlock();
}

/**
 * add the cores to batch_init_core_set and
 * add all the running batch jobs on the socket to this core
 */
void add_batch_cores(const set<int> &cores_to_add)
{
	// adjust mapping in batch_processes

	// adjust mapping in core_stats
	// djust mapping in socket_stats
	process_mtx.lock();
	core_stat_mtx.lock();
	socket_stat_mtx.lock();
	int socket_id;
	int socket_pos;
	for (auto core_id : cores_to_add)
	{
		core_id_to_socket(socket_layout, core_id, socket_id, socket_pos);
		for (auto batch_it = batch_processes.begin(); batch_it != batch_processes.end(); batch_it++)
		{
			auto pit = socket_stats[socket_id]->batch_pids.find(batch_it->pid);
			if (pit != socket_stats[socket_id]->batch_pids.end())
			{
				batch_it->add_cores_and_relocate_cores(cores_to_add);
				core_stat_mtx.lock();
				update_core_process_mapping(*batch_it);
				core_stat_mtx.unlock();
			}
		}
		socket_stats[socket_id]->add_batch_init_core(core_id);
	}
	socket_stat_mtx.unlock();
	core_stat_mtx.unlock();
	process_mtx.unlock();
}

void choose_and_throttle_batch_process(int socket_id)
{
	cout << "throttling cpu limit on socket " << socket_id << endl;
	process_mtx.lock();
	socket_stat_mtx.lock();
	float highest_limit = -2.0;
	set<process>::iterator candidate_it = batch_processes.end();
	// find the batch_process with the highest cpu limit
	cout << "number of running batch: " << batch_processes.size() << endl;
	for (auto batch_it = batch_processes.begin(); batch_it != batch_processes.end(); batch_it++)
	{
		auto candidate_pid_it = socket_stats[socket_id]->batch_pids.find(batch_it->pid);
		if (candidate_pid_it != socket_stats[socket_id]->batch_pids.end())
		{
			float cur_limit = batch_it->get_cpu_limit();
			if (cur_limit < 0)
			{
				highest_limit = cur_limit;
				candidate_it = batch_it;
				break;
			}
			else if (highest_limit < -1.5 ||
					 (highest_limit > 0 && cur_limit > highest_limit))
			{
				highest_limit = cur_limit;
				candidate_it = batch_it;
			}
		}
	}
	// we can unlock socket_stat_mtx here
	socket_stat_mtx.unlock();

	if (candidate_it == batch_processes.end())
	{
		cout << "no batch job is running. return normally" << endl;
		goto throttle_end;
	}

	if (highest_limit < 0)
	{
		highest_limit = CPU_THROTTLE_RATE * CORE_PER_SOCKET;
	}
	else
	{
		highest_limit *= CPU_THROTTLE_RATE;
	}
	cout << "process " << candidate_it->pid << " cgroup: " << candidate_it->full_cgroup_path << " is chosen. new limit: " << highest_limit << endl;
	int res;
	res = candidate_it->set_cpu_limit(highest_limit);
	if (res != 0)
	{
		cerr << "error in throttling cpu limit for pid: " << candidate_it->pid << endl;
	}

throttle_end:
	process_mtx.unlock();
}

void throttle_batch_processes_on_socket(int socket_id)
{
	cout << "throttling cpu limit of all batch processes on socket " << socket_id << endl;
	process_mtx.lock();
	socket_stat_mtx.lock();
	set<process>::iterator candidate_it = batch_processes.end();
	if (socket_stats[socket_id]->socket_limit < 0)
	{
		socket_stats[socket_id]->socket_limit = socket_stat::INIT_THROTTLE_RATE * CORE_PER_SOCKET;
	}
	else
	{
		socket_stats[socket_id]->socket_limit *= CPU_THROTTLE_RATE;
	}
	// calculate cpu limit for each process
	double process_limit = socket_stats[socket_id]->socket_limit / socket_stats[socket_id]->batch_pids.size();
	cout << "number of running batch: " << batch_processes.size() << endl;
	for (auto batch_it = batch_processes.begin(); batch_it != batch_processes.end(); batch_it++)
	{
		auto candidate_pid_it = socket_stats[socket_id]->batch_pids.find(batch_it->pid);
		if (candidate_pid_it != socket_stats[socket_id]->batch_pids.end())
		{
			int32_t res;
			res = batch_it->set_cpu_limit(process_limit);
			if (res != 0)
			{
				cerr << "error in throttling cpu limit for pid: " << batch_it->pid << endl;
			}
		}
	}
	socket_stat_mtx.unlock();
	process_mtx.unlock();
	usleep(AFTER_THROTTLE_INT);
}

/**
 * expand threads to unused core and its sibling
 * for all batch jobs
 * return the number of expaned batches
 */
void restore_all_batch_processes(set<int> cores)
{
	int count = 0;
	for (auto batch_it = batch_processes.begin(); batch_it != batch_processes.end(); batch_it++)
	{
		batch_it->add_cores_and_relocate_cores(cores);
	}
}

/**
	 a management thread
	 this is the core of the project
	 this thread is responsible for
	 1. keep track of latency-sensitive services and batch jobs
	 2. detect congestion
	 3. migrate thread of latency-sensitive services
	 4. throttle cpu usage of batch jobs
 */
void _manage_()
{
	cout << "management thread started" << endl;
	long lss_free_time[CORE_NUM];
	for (int i = 0; i < CORE_NUM; i++)
	{
		lss_free_time[i] = -1;
	}
	while (manage_enabled)
	//for (int i = 0; i < 1; i++)
	{
		// we go through cores one by one
		for (int core_id = 0; core_id < CORE_NUM; core_id++)
		{
			int thread_to_adjust_count = 0;
			int sibling_core_id = sibling(core_id);
			//cout << "socket event count: " << socket_stats[socket_id]->socket_event_count <<
			//" threshold: " << EVENT_THRESHOLD << endl;
			// TODO Add evt # as a threshold too!!
			if (core_stats[core_id]->hw_threshold > HW_METRIC_THRSHOLD)
			{
				//cout << "busy socket detected " << socket_id << " event count: " << socket_stats[socket_id]->socket_event_count <<
				//" threshold: " << EVENT_THRESHOLD << endl;

				// find possible lss to be adjusted
				set<process> lss_processes_to_adjust;
				// TODO, Test the function
				find_lss_to_adjust(core_id, lss_processes_to_adjust, thread_to_adjust_count);

				//cout << "done finding lss to adjust" << endl;

				// TODO: move this to a function
				// find candidate cores to migrate
				set<int> target_cores;
				if (thread_to_adjust_count > 0)
				{
					lss_free_time[core_id] = 0;
					cout << "finding target cores to migrate" << endl;
					// We do not schedule thread on sibling for simplicity
					for (int target_core_id = 0; target_core_id < CORE_NUM / 2; target_core_id++)
					{
						if (target_core_id == core_id
						 || target_core_id == sibling_core_id
						 || core_stats[target_core_id]->hw_threshold > HW_METRIC_THRSHOLD)
						{
							continue;
						}
						if (core_stats[target_core_id]->cpu_usage < CORE_IDLE_THRESHOULD && 
						core_stats[target_core_id]->lss_pids.size() == 0)
						{
							target_cores.insert(target_core_id);
						}
					}
				}
				else
				{
					// expand cores of batches to unused unreserved cores
					// if this core and sibling core does not have lss for a period of time
					// TODO test this function
					if (lss_free_time[core_id] >= 0 && lss_free_time[sibling_core_id] >= 0)
					{
						lss_free_time[core_id] += MANAGE_INT;
						if (lss_free_time[core_id] > RESTORE_THRESHOLD
						&& lss_free_time[sibling_core_id] > RESTORE_THRESHOLD)
						{
							cout << "restore batch cpu, busy" << endl;
							set<int> expanding_cores;
							expanding_cores.insert(core_id);
							expanding_cores.insert(sibling_core_id);
							restore_all_batch_processes(expanding_cores);
							lss_free_time[core_id] = -1;
							lss_free_time[sibling_core_id] = -1;
						}
					}
					continue;
				}
				// migrate thread
				cout << "migrating threads one by one" << endl;
				int target_core_count = target_cores.size();

				while (target_core_count >= thread_to_adjust_count && thread_to_adjust_count > 0)
				{
					/* we adjust the lss_process_to_adjust one by one */
					cout << "adjusting cores" << endl;
					for (auto it = lss_processes_to_adjust.begin(); it != lss_processes_to_adjust.end(); it++)
					{
						process p = *it;
						//it = lss_processes_to_adjust.erase(it);
						for (auto thread_it = p.thread_map.begin(); thread_it != p.thread_map.end(); thread_it++)
						{
							if (!thread_it->second.should_migrate)
							{
								continue;
							}
							int target_core = *target_cores.begin();
							target_cores.erase(target_cores.begin());

							/* migrate the thread thread_it to the core target_core */
							set<int> target_core_as_set;
							target_core_as_set.insert(target_core);
							// we need to update the core-process-thread info mapping too
							// unmap the old mapping. construct the new mapping
							// deal with the running batch thread on this core and
							remove_batch_cores(target_core_as_set);
							// adjust the batch_init_core_set
							p.set_thread_affinity(thread_it->second, target_core_as_set);
							thread_to_adjust_count--;
							target_core_count--;
						}
						// adjust core process mapping
						core_stat_mtx.lock();
						update_core_process_mapping(p);
						core_stat_mtx.unlock();

						socket_stat_mtx.lock();
						update_socket_process_mapping(p);
						socket_stat_mtx.unlock();
					}
				}
				// add the adjusted process back to lss_processes

				cout << "adding lss_to_adjust back to lss_processes" << endl;
				process_mtx.lock();
				for (auto it = lss_processes_to_adjust.begin(); it != lss_processes_to_adjust.end();)
				{
					lss_processes.insert(*it);
					it = lss_processes_to_adjust.erase(it);
				}
				process_mtx.unlock();

				// if we reach this point, it means all cores are busy
				// if the we still have threads to adjust and run out of idle cores
				// we need to shrink the cpu usage of batch jobs
				// we also need to remap thread-core mapping
				if (thread_to_adjust_count > 0)
				{
					if (!use_bulk_limit)
					{
						choose_and_throttle_batch_process(socket_id);
					}
					else
					{
						throttle_batch_processes_on_socket(socket_id);
					}
				}
			} // socket busy if statement
			else
			{
				// restore throttled batches on this socket to ulimited cpu usage
				// if this socket does not have lss for a period of time

				bool lss_free = true;
				for (auto pid_it = socket_stats[socket_id]->lss_pids.begin();
					 pid_it != socket_stats[socket_id]->lss_pids.end();
					 pid_it++)
				{
					set<process>::iterator lss_proc_it;
					for (lss_proc_it = lss_processes.begin(); lss_proc_it != lss_processes.end(); lss_proc_it++)
					{
						if (lss_proc_it->pid == *pid_it)
						{
							break;
						}
					}
					if (lss_proc_it != lss_processes.end())
					{
						if (lss_proc_it->cpu_usage > LSS_ACTIVE_THRESHOLD)
						{
							//cout << "lss pid: " << lss_proc_it->pid << " usage: " << lss_proc_it->cpu_usage << endl;
							lss_free = false;
							break;
						}
					}
				}
				if (lss_free && lss_free_time[socket_id] >= 0)
				{
					//cout << "socket: " << socket_id << " lss free time: " << lss_free_time[socket_id] << endl;
					lss_free_time[socket_id] += MANAGE_INT;
					if (lss_free_time[socket_id] > RESTORE_THRESHOLD)
					{
						cout << "restore batch cpu, idle" << endl;
						restore_all_batch_processes(socket_id);
						lss_free_time[socket_id] = -1;
					}
				}
				if (!lss_free && lss_free_time[socket_id] > 0)
				{
					lss_free_time[socket_id] = 0;
				}
			}
		} // socket iteration
		//cout << "finish a round of manage" << endl;

		usleep(MANAGE_INT);
		////report_manage();
	}
}

/* debug */
void report_profile()
{
	cout << "reporting per core event count and cpu usage" << endl;
	for (int i = 0; i < CORE_NUM; i++)
	{
		cout << "core: " << i << " count: " << core_stats[i]->event_count
			 << " core usage: " << core_stats[i]->cpu_usage << endl;
	}
	cout << "reporting per socket event count and cpu usage" << endl;
	for (int i = 0; i < SOCKET_NUM; i++)
	{
		cout << "socket: " << i << " count: " << socket_stats[i]->socket_event_count
			 << " socket usage: " << socket_stats[i]->socket_cpu_usage << endl;
	}
	if (lss_processes.size() > 0)
	{
		cout << "reporting lss processes cpu usages" << endl;
		for (auto it = lss_processes.begin(); it != lss_processes.end(); it++)
		{
			cout << "lss process id: " << it->pid << " cpu usage: " << it->cpu_usage << endl;
			for (auto iit = it->thread_map.begin(); iit != it->thread_map.end(); iit++)
			{
				cout << "lss thread id: " << iit->first << " cpu usage: " << iit->second.cpu_usage << endl;
			}
			cout << endl;
		}
	}
	if (batch_processes.size() > 0)
	{
		cout << "reporting batch processes cpu usage" << endl;
		for (auto it = batch_processes.begin(); it != batch_processes.end(); it++)
		{
			cout << "batch process id: " << it->pid << " cpu usage: " << it->cpu_usage << endl;
		}
	}
}

void report_stats()
{
	cout << "report socket_stats" << endl;
	for (int socket_id = 0; socket_id < SOCKET_NUM; socket_id++)
	{
		cout << "socket id: " << socket_id << " batch cores: " << endl;
		for (auto core_id : socket_stats[socket_id]->batch_init_cores)
		{
			cout << core_id << " ";
		}
		cout << endl
			 << "batch jobs: " << endl;
		for (auto pid : socket_stats[socket_id]->batch_pids)
		{
			cout << pid << " ";
		}
		cout << endl;
	}
	cout << "report core_stats" << endl;
	for (int core_id = 0; core_id < CORE_NUM; core_id++)
	{
		cout << "core: " << core_id << endl;
		if (core_stats[core_id]->batch_pids.size() > 0)
		{
			cout << "batch jobs: ";
			for (int pid : core_stats[core_id]->batch_pids)
			{
				cout << pid << " ";
			}
			cout << endl;
		}
		if (core_stats[core_id]->lss_pids.size() > 0)
		{
			cout << "lss: ";
			for (int pid : core_stats[core_id]->lss_pids)
			{
				cout << pid << " ";
			}
			cout << endl;
		}
	}
}

void report_batch()
{
	for (auto it = batch_processes.begin(); it != batch_processes.end(); it++)
	{
		cout << "pid: " << it->pid << " cgroup: " << it->full_cgroup_path << endl;
		cout << "thread map size shoud be 0. size: " << it->thread_map.size() << endl;
		cout << "core_thread_count: " << endl;
		for (auto entry : it->core_thread_count)
		{
			cout << "core: " << entry.first << " count: " << entry.second << endl;
		}
	}
}

void report_lss()
{
	cout << "reporting lss" << endl;
	for (auto it = lss_processes.begin(); it != lss_processes.end(); it++)
	{
		cout << "pid: " << it->pid << endl;
		cout << "thread map: " << endl;
		for (auto thread_it = it->thread_map.begin(); thread_it != it->thread_map.end(); thread_it++)
		{
			cout << "tid: " << thread_it->second.tid << " cpu usage: " << thread_it->second.cpu_usage << " migrate: " << thread_it->second.should_migrate << endl;
			cout << "cores: ";
			for (auto cit = thread_it->second.core_set.begin(); cit != thread_it->second.core_set.end(); cit++)
			{
				cout << *cit << " ";
			}
			cout << endl;
		}
		cout << endl;
		cout << "core_thread_count: " << endl;
		for (auto entry : it->core_thread_count)
		{
			cout << "core: " << entry.first << " count: " << entry.second << endl;
		}
	}
}

void report_manage()
{
	for (auto p : lss_processes)
	{
		cout << "lss pid: " << p.pid << endl;
		cout << "core set: ";
		for (auto i : p.core_thread_count)
		{
			cout << "core id: " << i.first << "thread count: " << i.second << endl;
			;
		}
		cout << endl;
	}
}

void report_cgroup()
{
	cout << "reporting current batch cgroups" << endl;
	/*
	for (auto group: current_batch_groups)
	{
		cout << "group name: " << group << endl;
	}
	*/
	for (auto process : batch_processes)
	{
		cout << "pid: " << process.pid
			 << " cgroup: " << process.full_cgroup_path
			 << " cpu_period: " << process.cpu_period
			 << " cpu_quota: " << process.cpu_quota
			 << " type: " << process.type
			 //<< " cpu_prev: " << process.cpu_prev
			 //<< " cpu_cur: " << process.cpu_cur
			 << " cpu_usage: " << process.cpu_usage
			 << endl;
	}
}

void test_set_lss_affinity()
{
	// test batch related mechanisms
	cout << "please enter pid of a latency-sensitive service" << endl;
	int lss_pid;
	cin >> lss_pid;
	incoming_pids_mtx.lock();
	incoming_pids.insert(lss_pid);
	incoming_pids_mtx.unlock();
	char dummy;
	cout << "press enter to report lss first time" << endl;
	dummy = getchar();
	dummy = getchar();
	report_lss();
	report_stats();

	set<process> to_adjust;
	int thread_count = 0;
	cout << "press enter to find lss thread to adjust" << endl;
	dummy = getchar();
	find_lss_to_adjust(0, to_adjust, thread_count);
	if (to_adjust.size() > 0)
	{
		cout << "following processes and threads should be migrated" << endl;
		for (auto p = to_adjust.begin(); p != to_adjust.end(); p++)
		{
			cout << "pid: " << p->pid << endl;
			;
			cout << "thread count: " << p->thread_map.size() << endl;
			for (auto t = p->thread_map.begin(); t != p->thread_map.end(); t++)
			{
				cout << "tid: " << t->second.tid << " core set: ";
				for (auto c : t->second.core_set)
				{
					cout << c << " ";
				}
				cout << endl
					 << "cpu usage: " << t->second.cpu_usage << " migrate? " << t->second.should_migrate << endl
					 << "tids: ";
				if (t->second.should_migrate)
				{
					cout << t->second.tid << " ";
				}
			}
			cout << endl;
		}
		cout << "press enter to adjust" << endl;
		dummy = getchar();
		set<int> target_cores;
		target_cores.insert(16);
		for (auto it = to_adjust.begin(); it != to_adjust.end();)
		{
			for (auto tit = it->thread_map.begin(); tit != it->thread_map.end(); tit++)
			{
				if (tit->second.should_migrate)
				{
					cout << "migrating pid: " << it->pid << " tid: " << tit->second.tid << endl;
					/* migrate the thread thread_it to the core target_core */
					// we need to update the core-process-thread info mapping too
					// unmap the old mapping. construct the new mapping
					// deal with the running batch thread on this core and
					remove_batch_cores(target_cores);
					// adjust the batch_init_core_set
					it->set_thread_affinity(tit->second, target_cores);
				}
			}
			core_stat_mtx.lock();
			update_core_process_mapping(*it);
			core_stat_mtx.unlock();
			process_mtx.lock();
			lss_processes.insert(*it);
			process_mtx.unlock();
			it = to_adjust.erase(it);
		}
	}
	else
	{
		cout << "nothing to adjust" << endl;
	}
	cout << "press enter to report lss second time" << endl;
	dummy = getchar();
	report_lss();
	report_stats();
}

void test_batch_functions()
{
	// test batch related mechanisms

	char dummy;
	cout << "press enter to first report" << endl;
	dummy = getchar();
	report_stats();
	report_batch();
	cout << "press enter to run function" << endl;
	dummy = getchar();
	set<int> to_modify;
	to_modify.insert(7);
	//remove_batch_cores(to_modity);
	//add_batch_cores(to_modify);
	for (int i = 0; i < 5; i++)
	{
		cout << "press to throttle cpu" << endl;
		dummy = getchar();
		choose_and_throttle_batch_process(0);
	}

	cout << "press enter to second report" << endl;
	dummy = getchar();
	report_stats();
	report_batch();
	cout << "press enter to exit" << endl;
	dummy = getchar();
}

int main(int argc, char **argv)
{
	int pid = getpid();
	cout << "pid: " << pid << " press enter to start" << endl;
	numa_conf = new conf("./numa.conf");
	cgroup_root = numa_conf->get_string("cgroup.root");
	cgroup_batch = numa_conf->get_string("cgroup.batch.group");
	cgroup_batch_root = cgroup_root + "cpu,cpuacct/" + cgroup_batch + "/";
	EVENT_SEC_THRESHOLD = numa_conf->get_int64("event_threshold");
	// restore threshold is in us
	RESTORE_THRESHOLD = numa_conf->get_int64("manage.restore_threshold");

	PROF_INT = numa_conf->get_int("profile.prof_us");
	IDLE_INT = numa_conf->get_int("profile.idle_us");
	MANAGE_INT = numa_conf->get_int("manage.interval_us");
	EVENT_THRESHOLD = EVENT_SEC_THRESHOLD / 1000000 * PROF_INT;

	getchar();
	// TODO wrap it in a init() function
	// initialize core_stats
	for (int i = 0; i < CORE_NUM; i++)
	{
		core_stat *cs = new core_stat(i);
		core_stats[i] = cs;
	}

	// initialize core allocation set
	int lss_num;
	lss_num = CORE_PER_SOCKET < 8 ? CORE_PER_SOCKET : 8;
	for (int socket_id = 0; socket_id < SOCKET_NUM; socket_id++)
	{
		socket_stat *socket_stat_p = new socket_stat(socket_id);
		socket_stats[socket_id] = socket_stat_p;
	}
	for (int i = 0; i < lss_num; i++)
	{
		socket_stats[0]->add_lss_init_core(i);
		lss_init_core_set.insert(socket_layout[0][i]);
	}
	for (int socket_id = 0, core = lss_num; socket_id < SOCKET_NUM; socket_id++)
	{
		socket_stat *socket_stat_p = socket_stats[socket_id];
		while (core < CORE_PER_SOCKET)
		{
			socket_stat_p->batch_init_cores.insert(socket_layout[socket_id][core]);
			core++;
		}
		core = 0;
	}

	profile_enabled = 1;
	manage_enabled = 1;
	thread profile_thread = thread(&_profile_);
	thread manage_thread = thread(&_manage_);
	while (1)
	{
		cout << "please enter pid of a latency-sensitive service" << endl;
		int pid;
		cin >> pid;
		incoming_pids_mtx.lock();
		incoming_pids.insert(pid);
		incoming_pids_mtx.unlock();
	}

	profile_enabled = 0;
	profile_thread.join();
	manage_enabled = 0;
	manage_thread.join();

	return 0;
}
