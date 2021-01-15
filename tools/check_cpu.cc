#include <fstream>
#include <iostream>
#include <thread>
#include <ctime>
#include <unistd.h>

#include "../core_stat.hpp"

using namespace std;

const string USAGE_FILE = "/home/epi/numa-project/tools/cpu-usage.txt";

const int32_t CORE_NUM = 64;
const int32_t SOCKET_NUM = 2;
const int32_t CORE_PER_SOCKET = 32;
int socket_layout[SOCKET_NUM][CORE_PER_SOCKET] =
    {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
      32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47},
     {16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
      48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63}};

core_stat global_core_stat;
core_stat *core_stats[CORE_NUM];

void _checkStat_()
{
    ofstream cpu_usage_file = ofstream();
    cpu_usage_file.open(USAGE_FILE, ios::app);
		int t = 0;
    while (t < 3800)
    {
        ifstream stat_file = ifstream();
        string stat_line;

        stat_file.open(STAT_FILE);
        stat_file.seekg(0, std::ios::beg);
        std::getline(stat_file, stat_line);
        global_core_stat.set_time(stat_line);
        for (int i = 0; i < CORE_NUM; i++)
        {
            core_stats[i]->prev_idle_time = core_stats[i]->idle_time;
            core_stats[i]->prev_running_time = core_stats[i]->running_time;
            std::getline(stat_file, stat_line);
            core_stats[i]->set_time(stat_line);
        }
        stat_file.close();

        double sockets[2] = {0.0, 0.0};
        for (int socket_id = 0; socket_id < SOCKET_NUM; socket_id++)
        {
            double socket_usage = 0.0;
            uint64_t socket_count = 0;
            for (int socket_pos = 0; socket_pos < CORE_PER_SOCKET; socket_pos++)
            {
                socket_usage += core_stats[socket_layout[socket_id][socket_pos]]->cpu_usage;
                socket_count += core_stats[socket_layout[socket_id][socket_pos]]->event_count;
            }
            sockets[socket_id] = socket_usage;
        }
        time_t now = time(0);
        string dt = ctime(&now);
        cout << "time: " << dt
             << "socket0: " << sockets[0]
             << " socket1: " << sockets[1] << endl;
        cpu_usage_file << "time: " << dt
                       << "socket0: " << sockets[0]
                       << " socket1: " << sockets[1] << endl;
        sleep(1);
				t++;
    }
}

int main()
{
    for (int i = 0; i < CORE_NUM; i++)
    {
        core_stat *cs = new core_stat(i);
        core_stats[i] = cs;
    }
    thread profile_thread = thread(&_checkStat_);

    profile_thread.join();
    return 0;
}
