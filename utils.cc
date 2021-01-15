#include <sys/time.h>
#include <iostream>
#include "utils.hpp"

suseconds_t get_interval(struct timeval start, struct timeval end)
{
	return (end.tv_sec - start.tv_sec)*1000000+(end.tv_usec-start.tv_usec);
}

long get_interval_ns(struct timespec start, struct timespec end) {
	return (long)(end.tv_sec - start.tv_sec)*1000000000+(end.tv_nsec-start.tv_nsec);
}

void core_id_to_socket(int socket_layout[][CORE_PER_SOCKET], int core_id, int &socket_id, int &socket_pos)
{
	for (int s = 0; s < SOCKET_NUM; s++)
	{
		for (int c = 0; c < CORE_PER_SOCKET; c++)
		{
			if (socket_layout[s][c] == core_id)
			{
				socket_id = s;
				socket_pos = c;
				return;
			}
		}
	}
	std::cerr << "cannot find socket of the core_id: " << core_id << std::endl;
}

void socket_to_core_id(int socket_layout[][CORE_PER_SOCKET], int socket_id, int socket_pos, int &core_id)
{
	if (socket_id >= SOCKET_NUM || socket_pos >= CORE_PER_SOCKET)
	{
		std::cerr << "cannot find core_id of the socket and possition: " << socket_id << ", " << socket_pos << std::endl;
		return;
	}
	core_id = socket_layout[socket_id][socket_pos];
}

int sibling(int core)
{
	if (core < 32)
	{
		return core + 32;
	}
	else
	{
		return core - 32;
	}
	
}