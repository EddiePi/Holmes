#ifndef PEBS_EVENT_HPP_
#define PEBS_EVENT_HPP_
#include <string.h>
#include <sys/ioctl.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <asm/unistd.h>

void perf_event_init(struct perf_event_attr* event, uint64_t event_no);
int perf_event_open(struct perf_event_attr* event, 
      pid_t pid, int cpu, int groiup_fd, unsigned long flags);
int perf_event_enable(int event_file);
int perf_event_disable(int event_file);
int perf_event_reset(int event_file);

#endif // PEBS_EVENT_HPP_
