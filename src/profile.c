#include "internal.h"
#include "profile.h"

#include <unistd.h>
#include <sys/time.h>
#include <pthread.h>
#include <errno.h>
#include <poll.h>
#include <linux/perf_event.h>
#include <asm/perf_regs.h>
#include <asm/unistd.h>
#include <perfmon/pfmlib_perf_event.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

internal void profile_init(void) {
    doing_profiling     = 1;
    data.thread_started = 1;

    LOG("initialized profiling thread\n");
}
