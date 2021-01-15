#ifndef SOCKET_STAT_HPP_
#define SOCKET_STST_HPP_
#include <set>
#include <cstdint>

using std::set;

class socket_stat
{
public:
	int socket_id;
	double socket_cpu_usage;
	double socket_limit;
	uint64_t socket_event_count;
	set<int> batch_init_cores;
	set<int> lss_init_cores;
	set<int> batch_pids;
	set<int> lss_pids;

	static constexpr double INIT_THROTTLE_RATE = 0.5;

	socket_stat(int socket_id);
	void remove_batch_init_core(int core_id);
	void add_batch_init_core(int core_id);
	void remove_lss_init_core(int core_id);
	void add_lss_init_core(int core_id);
};
#endif // SOCKET_STAT_HPP_
