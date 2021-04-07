#include "msg.h"
#define HMALLOC_MSG_SENDER
#define HMALLOC_MSG_IMPL
#include "hmalloc_msg.h"

internal hmalloc_msg_sender msg_sender;
internal pid_t              pid;

internal void msg_init(void) {
    int         err;
    hmalloc_msg init_msg;

    LOG("sending messages to a profiler\n");

    err = hmsg_start_sender(&msg_sender);
    if (err != 0) {
        hmalloc_msg_mode = 0;
        LOG("hmsg_start_sender() failed with error %d\n", err);
    }
    ASSERT(err == 0, "hmsg_start_sender() failed");
    LOG("initialized connection to socket\n");

    if (hmalloc_msg_mode) {
        pid = getpid();
        init_msg.header.msg_type = HMSG_INIT;
        init_msg.header.pid   = pid;
        if (hmalloc_msg_mode == HMALLOC_MSG_MODE_OBJECT) {
            init_msg.init.mode  = HMSG_MODE_OBJECT;
        } else if (hmalloc_msg_mode == HMALLOC_MSG_MODE_USER_HEAP) {
            init_msg.init.mode  = HMSG_MODE_SITE_BLOCK;
        }

        err = hmsg_send(&msg_sender, &init_msg);

        if (err < 0) {
            hmalloc_msg_mode = 0;
            LOG("hmsg_send() failed with error %d\n", err);
        }
        ASSERT(err == 0, "hmsg_send() failed");
        LOG("sent initial message\n");
    }
}

internal void msg_fini(void) {
    hmalloc_msg fini_msg;
    int         err;

    fini_msg.header.msg_type = HMSG_FINI;
    fini_msg.header.pid      = pid;

    err = hmsg_send(&msg_sender, &fini_msg);

    if (err < 0) {
        LOG("hmsg_send() failed with error %d\n", err);
    }
    ASSERT(err == 0, "hmsg_send() failed");

    hmsg_close_sender(&msg_sender);
}

internal void msg_obj_alloc(void* addr, u64 size, heap_t *heap) {
    struct timespec ts;
    hmalloc_msg     allo_msg;
    int             err;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    allo_msg.header.msg_type   = HMSG_ALLO;
    allo_msg.header.pid        = pid;
    allo_msg.allo.addr         = (u64)addr;
    allo_msg.allo.size         = size;
    allo_msg.allo.timestamp_ns = 1000000000ULL * ts.tv_sec + ts.tv_nsec;
    allo_msg.allo.hid          = (heap->__meta.hid << 1) | (heap->__meta.flags & HEAP_USER);

    err = hmsg_send(&msg_sender, &allo_msg);

    if (err < 0) {
        LOG("hmsg_send() failed with error %d\n", err);
    }
    ASSERT(err == 0, "hmsg_send() failed");
}

internal void msg_obj_free(void* addr) {
    struct timespec ts;
    hmalloc_msg     free_msg;
    int             err;

    if (hmalloc_ignore_frees) { return; }

    clock_gettime(CLOCK_MONOTONIC, &ts);

    free_msg.header.msg_type   = HMSG_FREE;
    free_msg.header.pid        = pid;
    free_msg.free.addr         = (u64)addr;
    free_msg.free.timestamp_ns = 1000000000ULL * ts.tv_sec + ts.tv_nsec;

    err = hmsg_send(&msg_sender, &free_msg);

    if (err < 0) {
        LOG("hmsg_send() failed with error %d\n", err);
    }
    ASSERT(err == 0, "hmsg_send() failed");
}
