#include <fstream>
#include <string>
#include <sstream>
#include "core_stat.hpp"
#include <iostream>

using std::cout;
using std::cerr;
using std::endl;
using std::istringstream;
using std::string;

core_stat::core_stat()
{
	this->core_id = 0;
	this->prev_running_time = 0;
	this->prev_idle_time = 0;
	this->running_time = 0;
	this->idle_time = 0;
	this->event_count = 0;
	this->cpu_usage = 0.0;
}

core_stat::core_stat(int id)
{
	this->core_id = id;
	this->prev_running_time = 0;
	this->prev_idle_time = 0;
	this->running_time = 0;
	this->idle_time = 0;
	this->cpu_usage = 0.0;
}

uint64_t core_stat::get_interval()
{
	uint64_t interval = 0;
	if (this->running_time >= this->prev_running_time &&
				this->idle_time >= this->prev_idle_time)
	{
		interval = running_time + idle_time - prev_running_time - prev_idle_time;
	}
	return interval;
}

void core_stat::set_prev_time(string line)
{
	istringstream iss(line);
	string dummy;
	uint64_t cur;

	this->prev_running_time = 0;
	this->prev_idle_time = 0;
	iss >> dummy;
	for (int i = 0; i < NUM_CPU_STATES; i++)
	{
		iss >> cur;
		if (i != 3 && i !=4)
		{
			this->prev_running_time += cur;
		}
		else
		{
			this->prev_idle_time += cur;
		}
	}
}


void core_stat::set_time(string line)
{
	istringstream iss(line);
	string dummy;
	uint64_t cur;

	this->running_time = 0;
	this->idle_time = 0;
	iss >> dummy;
	for (int i = 0; i < NUM_CPU_STATES; i++)
	{
		iss >> cur;
		if (i != 3 && i !=4)
		{
			this->running_time += cur;
		}
		else
		{
			this->idle_time += cur;
		}
	}
	set_cpu_usage();
}

double core_stat::get_cpu_usage()
{
	return this->cpu_usage;
}

uint64_t core_stat::get_event_count()
{
	return this->event_count;
}

void core_stat::set_cpu_usage()
{
	if (this->running_time < this->prev_running_time ||
				this->idle_time < this->prev_idle_time)
	{
		this->cpu_usage = -1.0;
	}
	else 
	{
		double running_delta = this->running_time - this->prev_running_time;
		double idle_delta = this->idle_time - this->prev_idle_time;
		this->cpu_usage = running_delta / (running_delta + idle_delta);
	}
}

void core_stat::add_batch_pid(int pid)
{
	this->batch_pids.insert(pid);
}

void core_stat::remove_batch_pid(int pid)
{
	auto it = batch_pids.find(pid);
	if (it != batch_pids.end())
	{
		batch_pids.erase(it);
	}
}

void core_stat::clear_batch_pids()
{
	batch_pids.clear();
}

void core_stat::add_lss_pid(int pid)
{
	this->lss_pids.insert(pid);
}


void core_stat::remove_lss_pid(int pid)
{
	auto it = lss_pids.find(pid);
	if (it != lss_pids.end())
	{
		lss_pids.erase(it);
	}
}

void core_stat::clear_lss_pids()
{
	lss_pids.clear();
}
