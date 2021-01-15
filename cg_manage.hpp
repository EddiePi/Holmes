#ifndef CG_MANAGE_HPP_
#define CG_MANAGE_HPP_

#include "conf.hpp"

/*
#define CG_ROOT_DIR "/sys/fs/cgroup/"
#define CG_CPU_DIR "cpu,cpuacct/"
#define CG_CPU_PERIOD_FILE "cpu.cfs_period_us"
#define CG_CPU_QUOTA_FILE "cpu.cfs_quota_us"
*/
class cg_manager
{
	public:
		static const string CG_ROOT_DIR;
		static const string CG_CPU_DIR;
		static const string CG_CPU_PERIOD_FILE;
		static const string CG_CPU_QUOTA_FILE;
		cg_manager(conf *numa_conf);
		int set_cpu_limit(string group_name, float percent);

	private:
		conf *numa_conf;
		unsigned int period;
		unsigned int quota;
};
	

#endif // CG_MANAGE_HPP_
