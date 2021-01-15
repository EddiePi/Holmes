#include <fstream>
#include <iostream>
#include <string>
#include "cg_manage.hpp"

using std::ofstream;
using std::cout;
using std::cerr;
using std::endl;

const string cg_manager::CG_ROOT_DIR = "/sys/fs/cgroup/";
const string cg_manager::CG_CPU_DIR = "cpu,cpuacct/";
const string cg_manager::CG_CPU_PERIOD_FILE = "cpu.cfs_period_us";
const string cg_manager::CG_CPU_QUOTA_FILE = "cpu.cfs_quota_us";

cg_manager::cg_manager(conf *numa_conf)
{
	this->numa_conf = numa_conf;
	this->period = this->numa_conf->get_int("cgroup.cpu.period");
	this->quota = this->period;
}

int cg_manager::set_cpu_limit(string group_name, float percent)
{
	string full_group_path;
	string full_group_file_path;
	group_name.erase(0, group_name.find_first_not_of("/"));
	group_name.erase(group_name.find_last_not_of("/") + 1);
	full_group_path = CG_ROOT_DIR + CG_CPU_DIR + group_name +"/";
	full_group_file_path = full_group_path + CG_CPU_QUOTA_FILE;

	this->quota = (int) this->period * percent;
	ofstream quota_file(full_group_file_path);
	if (!quota_file.is_open())
	{
		cerr << "write " << full_group_file_path << " failed" << endl;
		return -1;
	}
	cout << "writing cgroup file " << full_group_file_path 
		<< "value: " << this->quota << endl;
	quota_file << this->quota;
	quota_file.close();
	return 0;
}
