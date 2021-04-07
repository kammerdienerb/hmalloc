#ifndef __HMALLOC_MSG_H__
#define __HMALLOC_MSG_H__

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>        /* For mode constants */
#include <mqueue.h>

#define HMSG_MQ_NAME "/hmsg.mq"
#define HMSG_PRIO    (0)

enum {
    HMSG_INIT,
    HMSG_FINI,
    HMSG_ALLO,
    HMSG_FREE,
};

enum {
    HMSG_MODE_OBJECT,
    HMSG_MODE_SITE_BLOCK,
};

typedef struct __attribute__((packed)) {
    uint8_t msg_type;
    pid_t   pid;
} hmalloc_msg_header;

typedef struct __attribute__((packed)) {
    uint8_t mode;
} hmalloc_init_msg;

typedef struct __attribute__((packed)) {
} hmalloc_fini_msg;

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint64_t size;
    uint64_t timestamp_ns;
    uint32_t hid;
} hmalloc_allo_msg;

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint64_t timestamp_ns;
} hmalloc_free_msg;

typedef struct __attribute__((packed)) {
    hmalloc_msg_header   header;
    union {
        hmalloc_init_msg init;
        hmalloc_fini_msg fini;
        hmalloc_allo_msg allo;
        hmalloc_free_msg free;
    };
} hmalloc_msg;



#ifdef HMALLOC_MSG_SENDER

typedef struct {
    mqd_t qd;
} hmalloc_msg_sender;

int hmsg_start_sender(hmalloc_msg_sender *sender);
int hmsg_close_sender(hmalloc_msg_sender *sender);
int hmsg_send(hmalloc_msg_sender *sender, hmalloc_msg *msg);

#endif /* HMALLOC_MSG_SENDER */

#ifdef HMALLOC_MSG_RECEIVER

typedef struct {
    mqd_t qd;
} hmalloc_msg_receiver;

int hmsg_start_receiver(hmalloc_msg_receiver *receiver);
int hmsg_close_receiver(hmalloc_msg_receiver *receiver);
int hmsg_receive(hmalloc_msg_receiver *receiver, hmalloc_msg *msg);

#endif /* HMALLOC_MSG_RECEIVER */


#ifdef HMALLOC_MSG_IMPL

static uint32_t _hmsg_size(hmalloc_msg *msg) {
    uint32_t size;

    size = sizeof(hmalloc_msg_header);

    switch (msg->header.msg_type) {
        case HMSG_INIT:
            size += sizeof(hmalloc_init_msg);
            break;
        case HMSG_FINI:
            size += sizeof(hmalloc_fini_msg);
            break;
        case HMSG_ALLO:
            size += sizeof(hmalloc_allo_msg);
            break;
        case HMSG_FREE:
            size += sizeof(hmalloc_free_msg);
            break;
        default:
            size = 0;
    }

    return size;
}

#ifdef HMALLOC_MSG_SENDER

int hmsg_start_sender(hmalloc_msg_sender *sender) {
    int err;

    err = 0;

    if ((sender->qd = mq_open(HMSG_MQ_NAME, O_WRONLY)) == -1) {
        err   = -errno;
        errno = 0;
        goto out;
    }

out:;
    return err;
}

int hmsg_close_sender(hmalloc_msg_sender *sender) { return mq_close(sender->qd); }

int hmsg_send(hmalloc_msg_sender *sender, hmalloc_msg *msg) {
    int err;

    err = mq_send(sender->qd, (const char*)msg, _hmsg_size(msg), HMSG_PRIO);

    if (err == -1) {
        err   = -errno;
        errno = 0;
        goto out;
    }

out:;
    return err;
}

#endif /* HMALLOC_MSG_SENDER */

#ifdef HMALLOC_MSG_RECEIVER

int hmsg_start_receiver(hmalloc_msg_receiver *receiver) {
    int            err;
    struct mq_attr attr;

    err = 0;

    mq_unlink(HMSG_MQ_NAME);

    attr.mq_maxmsg  = 10;
    attr.mq_msgsize = sizeof(hmalloc_msg);

    if ((receiver->qd = mq_open(HMSG_MQ_NAME, O_RDONLY | O_CREAT | O_EXCL, 0664, &attr)) == -1) {
        err   = -errno;
        errno = 0;
        goto out;
    }

out:;
    return err;
}

int hmsg_close_receiver(hmalloc_msg_receiver *receiver) {
    int err;

    err = mq_close(receiver->qd);
    if (err == -1) {
        err   = -errno;
        errno = 0;
    }

    mq_unlink(HMSG_MQ_NAME);

    return err;
}

int hmsg_receive(hmalloc_msg_receiver *receiver, hmalloc_msg *msg) {
    int err;

    err = mq_receive(receiver->qd, (char*)msg, sizeof(*msg), NULL);

    if (err == -1) {
        err   = -errno;
        errno = 0;
    }

    return err;
}

#endif /* HMALLOC_MSG_RECEIVER */
#endif /* HMALLOC_MSG_IMPL */

#endif
