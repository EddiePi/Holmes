#include <cstdlib>
#include <iostream>
#include <unistd.h>
#include <errno.h>
#include <sys/syscall.h>
#include "pebs_event.hpp"

using std::cout;
using std::endl;
using std::cerr;

void perf_event_init(struct perf_event_attr* pe, uint64_t event_no)
{
    memset(pe, 0, sizeof(struct perf_event_attr));
    pe->type = PERF_TYPE_RAW;
    pe->size = sizeof(struct perf_event_attr);
    pe->config = event_no;
    pe->disabled = 1;
    pe->exclude_kernel = 0;
    pe->exclude_hv = 0;
    pe->read_format = PERF_FORMAT_TOTAL_TIME_ENABLED |
        PERF_FORMAT_TOTAL_TIME_RUNNING;
}

int perf_event_open(struct perf_event_attr *pe, pid_t pid,
    int cpu, int group_fd, unsigned long flags)
{
    int ret;
    ret = syscall(__NR_perf_event_open, pe, pid, cpu, group_fd, flags);

    return ret;
}

int perf_event_enable(int event_file)
{
    int ret;
    ret = ioctl(event_file, PERF_EVENT_IOC_ENABLE, 0);

    return ret;
}

int perf_event_disable(int event_file)
{
    int ret;
    ret = ioctl(event_file, PERF_EVENT_IOC_DISABLE, 0);

    return ret;
}

int perf_event_reset(int event_file)
{
    int ret;
    ret = ioctl(event_file, PERF_EVENT_IOC_RESET, 0);

    return ret;
}
