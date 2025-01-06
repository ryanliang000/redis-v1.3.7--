/* Select()-based ae.c module
 * Copyright (C) 2009-2010 Salvatore Sanfilippo - antirez@gmail.com
 * Released under the BSD license. See the COPYING file for more info. */

#include <string.h>

// 使用select时的状态记录结构体
typedef struct aeApiState {
    // 读取的read-fds、写入的write-fds
    fd_set rfds, wfds;
    /* We need to have a copy of the fd sets as it's not safe to reuse
     * FD sets after select(). */
    // 复制的read-fds, write-fds
    fd_set _rfds, _wfds;
} aeApiState;

// 创建eventloop的apidata，指向state
static int aeApiCreate(aeEventLoop *eventLoop) {
    // 创建select使用的apiState
    aeApiState *state = zmalloc(sizeof(aeApiState));

    // 清理read-fds, write-fds状态位
    if (!state) return -1;
    FD_ZERO(&state->rfds);
    FD_ZERO(&state->wfds);

    // 把state设定到eventloop中
    eventLoop->apidata = state;
    return 0;
}

// free掉eventloop上的apidata
static void aeApiFree(aeEventLoop *eventLoop) {
    zfree(eventLoop->apidata);
}

// 设定监听某个fd的读取或写入
static int aeApiAddEvent(aeEventLoop *eventLoop, int fd, int mask) {
    // 转apistate指针
    aeApiState *state = eventLoop->apidata;

    // 基于mask，添加监听某个fd的read或write
    if (mask & AE_READABLE) FD_SET(fd,&state->rfds);
    if (mask & AE_WRITABLE) FD_SET(fd,&state->wfds);
    return 0;
}

// 去除监听某个fd的读取或写入
static void aeApiDelEvent(aeEventLoop *eventLoop, int fd, int mask) {
    aeApiState *state = eventLoop->apidata;

    // 基于mask, 去除监听某个fd的read或write
    if (mask & AE_READABLE) FD_CLR(fd,&state->rfds);
    if (mask & AE_WRITABLE) FD_CLR(fd,&state->wfds);
}

static int aeApiPoll(aeEventLoop *eventLoop, struct timeval *tvp) {
    aeApiState *state = eventLoop->apidata;
    int retval, j, numevents = 0;
    
    // 把rfds复制到_rfds，把wfds复制到_wfds，用于select查询
    memcpy(&state->_rfds,&state->rfds,sizeof(fd_set));
    memcpy(&state->_wfds,&state->wfds,sizeof(fd_set));

    // 查询接口read和write状态位, 等待时间使用传入timeval
    retval = select(eventLoop->maxfd+1,
                &state->_rfds,&state->_wfds,NULL,tvp);
    if (retval > 0) {
        // 遍历fd，检查状态位的设置情况
        for (j = 0; j <= eventLoop->maxfd; j++) {
            int mask = 0;
            aeFileEvent *fe = &eventLoop->events[j];
            
            // 同时检查eventLoop->events上的设置与select获取到的结果；
            // 同时符合时，把结果记录到eventLoop->fired数组中;
            // 记录fd和mask到fired信息中；
            if (fe->mask == AE_NONE) continue;
            if (fe->mask & AE_READABLE && FD_ISSET(j,&state->_rfds))
                mask |= AE_READABLE;
            if (fe->mask & AE_WRITABLE && FD_ISSET(j,&state->_wfds))
                mask |= AE_WRITABLE;
            eventLoop->fired[numevents].fd = j;
            eventLoop->fired[numevents].mask = mask;
            numevents++;
        }
    }
    return numevents;
}

// 返回api名称"select"
static char *aeApiName(void) {
    return "select";
}
