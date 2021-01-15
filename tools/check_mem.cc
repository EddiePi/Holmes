#include <fstream>
#include <iostream>
#include <thread>
#include <ctime>
#include <sstream>
#include <unistd.h>

using namespace std;

const string USAGE_FILE = "/home/epi/numa-project/tools/mem-usage.txt";
const string STAT_FILE = "/proc/meminfo";

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
        std::getline(stat_file, stat_line);
        stat_file.close();
        stringstream ss(stat_line);
        string dummy;
        long mem_free;
        ss >> dummy;
        ss >> mem_free;

        time_t now = time(0);
        string dt = ctime(&now);
        cout << "time: " << dt
             << "mem free: " << mem_free << endl;
        cpu_usage_file << "time: " << dt
                       << "mem_free: " << mem_free << endl;
        sleep(1);
        t++;
    }
}

int main()
{
    thread profile_thread = thread(&_checkStat_);

    profile_thread.join();
    return 0;
}
