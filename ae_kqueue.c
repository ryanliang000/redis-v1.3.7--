/* Kqueue(2)-based ae.c module
 * Copyright (C) 2009 Harish Mallipeddi - harish.mallipeddi@gmail.com
 * Released under the BSD license. See the COPYING file for more info. */

#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>

// kqueue使用的state和epoll内容相似，是一个kqfd监听fd，与kevent收集的监听事件
typedef struct aeApiState {
    int kqfd;
    struct kevent events[AE_SETSIZE];
} aeApiState;

// 创建监听：先分配创建state结构体，然后创建kqueue链接，把fd到state->kqfd上，把state指向到eventLoop->apidata上
static int aeApiCreate(aeEventLoop *eventLoop) {
    aeApiState *state = zmalloc(sizeof(aeApiState));

    if (!state) return -1;
    state->kqfd = kqueue();
    if (state->kqfd == -1) return -1;
    eventLoop->apidata = state;
    
    return 0;    
}

// 释放监听：把kqueue关闭了，然后释放掉state
static void aeApiFree(aeEventLoop *eventLoop) {
    aeApiState *state = eventLoop->apidata;

    close(state->kqfd);
    zfree(state);
}

// 添加事件
static int aeApiAddEvent(aeEventLoop *eventLoop, int fd, int mask) {
    aeApiState *state = eventLoop->apidata;
    struct kevent ke;
    
    // kqueue的读写事件是分开的，和epoll不同，epool是一次设一个fd
    // 监测有读事件监听时，使用EV_SET设置kevent添加，然后使用kevent函数设置上去
    if (mask & AE_READABLE) {
        EV_SET(&ke, fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
        if (kevent(state->kqfd, &ke, 1, NULL, 0, NULL) == -1) return -1;
    }
    // 监测有写事件监听时，使用EV_SET设置kevent添加，然后使用kevent函数设置上去
    if (mask & AE_WRITABLE) {
        EV_SET(&ke, fd, EVFILT_WRITE, EV_ADD, 0, 0, NULL);
        if (kevent(state->kqfd, &ke, 1, NULL, 0, NULL) == -1) return -1;
    }
    return 0;
}

static void aeApiDelEvent(aeEventLoop *eventLoop, int fd, int mask) {
    aeApiState *state = eventLoop->apidata;
    struct kevent ke;
    
    // kqueue的读写事件是分开的，所以删除时，也是各是各的；和epoll不同，epool是读写耦合在一起的；
    // 监测要删除读监听时，使用EV_SET设置kevent删除，然后使用kevent函数设置上去
    if (mask & AE_READABLE) {
        EV_SET(&ke, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
        kevent(state->kqfd, &ke, 1, NULL, 0, NULL);
    }
    // 监测要删除写监听时，使用EV_SET设置kevent删除，然后使用kevent函数设置上去
    if (mask & AE_WRITABLE) {
        EV_SET(&ke, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
        kevent(state->kqfd, &ke, 1, NULL, 0, NULL);
    }
}

static int aeApiPoll(aeEventLoop *eventLoop, struct timeval *tvp) {
    aeApiState *state = eventLoop->apidata;
    int retval, numevents = 0;

    // 对于timeval，kqueue使用的结构体是不同的，使用的timespec结构体，需要进行转换后使用；
    // timeval是存sec和usec，timespec是存sec和nsec，需要转换一下;
    // 下面分别处理了带等待时间的，还不带等待时间的
    if (tvp != NULL) {
        struct timespec timeout;
        timeout.tv_sec = tvp->tv_sec;
        timeout.tv_nsec = tvp->tv_usec * 1000;
        retval = kevent(state->kqfd, NULL, 0, state->events, AE_SETSIZE, &timeout);
    } else {
        retval = kevent(state->kqfd, NULL, 0, state->events, AE_SETSIZE, NULL);
    }    

    // 监听到的事件
    if (retval > 0) {
        int j;
        
        // 把监听到的事件从state->events数组中取出，放入到eventLoop->fired数组中去。
        numevents = retval;
        for(j = 0; j < numevents; j++) {
            int mask = 0;
            struct kevent *e = state->events+j;
            
            if (e->filter == EVFILT_READ) mask |= AE_READABLE;
            if (e->filter == EVFILT_WRITE) mask |= AE_WRITABLE;
            eventLoop->fired[j].fd = e->ident; 
            eventLoop->fired[j].mask = mask;           
        }
    }

    // 返回监听到的事件数量
    return numevents;
}

// api名称使用kqueue
static char *aeApiName(void) {
    return "kqueue";
}
