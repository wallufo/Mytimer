#pragma once
#ifndef __MYTIMER_H
#define __MYTIMER_H

#include<pthread.h>
#include<stdio.h>
#include "uthash.h"
#define DEBUG 0
#if DEBUG
// Truncates the full __FILE__ path, only displaying the basename
#define __FILENAME__ \
    (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

// Convenient macros for printing out messages to the kernel log buffer
#define mytimer_err(fmt, ...) \
    printf(" [ERROR] : %s: %s: %d: " fmt, __FILENAME__, __func__, \
           __LINE__, ## __VA_ARGS__)
#define mytimer_info(fmt, ...) \
    printf(" [INFO ] : %s: %s: %d: " fmt, __FILENAME__, __func__, \
            __LINE__, ## __VA_ARGS__)
#else
#define mytimer_err(fmt, ...)
#define mytimer_info(fmt, ...)
#endif
typedef int(*mytimer_callback)(void*);
typedef struct __mytimer
{
	int              mytimer_fd;
	int              mytimer_count;
    double           mytimer_interval;
    void*            mytimer_data;
    UT_hash_handle   hh;
	mytimer_callback mytimer_cb;
}mytimer;

typedef struct __mytimer_handle
{
    int              mytimer_handle_epoll_fd;
    int              mytimer_handle_activate;
    int              mytimer_handle_fd_max;
    pthread_t        mytimer_handle_thread_id;
    pthread_mutex_t  mytimer_handle_mutex;
    mytimer*         mytimer_handle_node;
}mytimer_handle;

static mytimer _InitTime = {
    .mytimer_count = -99,
    .mytimer_data = NULL,
    .mytimer_fd = -1,
    .mytimer_interval = -1,
    .mytimer_cb = NULL,
};
mytimer_handle* mytimer_init(int max_num);
int mytimer_add(mytimer_handle* _mth, mytimer* _timer);
int mytimer_count(mytimer_handle* _mth);
//TODO: 或许可以减少mytimer结构体的暴露
//int mytimer_add(mytimer_handle* _mth, double time_interval, int counts, mytimer_callback _cb, void* arg);
int mytimer_del(mytimer_handle* _mth, mytimer* _timer);
int mytimer_des(mytimer_handle* _mth);
#endif