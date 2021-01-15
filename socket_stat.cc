#include <set>

#include "socket_stat.hpp"

socket_stat::socket_stat(int socket_id)
{
	this->socket_id = socket_id;
	this->socket_cpu_usage = 0.0;
	this->socket_event_count = 0;
	this->socket_limit = -1.0;
}

void socket_stat::remove_batch_init_core(int core_id)
{
	auto it = this->batch_init_cores.find(core_id);
	if (it != this->batch_init_cores.end())
	{
		this->batch_init_cores.erase(it);
	}
}

void socket_stat::add_batch_init_core(int core_id)
{
	this->batch_init_cores.insert(core_id);
}

void socket_stat::remove_lss_init_core(int core_id)
{
	auto it = this->lss_init_cores.find(core_id);
	if (it != this->lss_init_cores.end())
	{
		this->lss_init_cores.erase(it);
	}
}

void socket_stat::add_lss_init_core(int core_id)
{
	this->lss_init_cores.insert(core_id);
}
