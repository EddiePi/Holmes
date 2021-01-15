#ifndef UTILS_HPP_
#define UTILS_HPP_
#include <sys/time.h>

const int32_t CORE_NUM = 64;
const int32_t SOCKET_NUM = 2;
const int32_t CORE_PER_SOCKET = 32;

suseconds_t get_interval(struct timeval start, struct timeval end);

long get_interval_ns(struct timespec start, struct timespec end);

void core_id_to_socket(int socket_layout[][CORE_PER_SOCKET], int core_id, int &socket_id, int &socket_pos);

void socket_to_core_id(int socket_layout[][CORE_PER_SOCKET], int socket_id, int socket_pos, int &core_id);

int sibling(int core);

#endif // UTILS_HPP_
