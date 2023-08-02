#define _POSIX_C_SOURCE 199309L 
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include "mytimer.h"

#define MAXNUM 32
/// <summary>
/// 保活任务
/// </summary>
/// <param name="arg"></param>
/// <returns></returns>
static int KeepAlive(void* arg) {
	mytimer_info("keep alive\r\n");
	return 0;
}
/// <summary>
/// 设置fd为非阻塞状态
/// </summary>
/// <param name="fd"></param>
/// <returns></returns>
static int SetNonBlock(int fd)
{
	//通过fcntl的F_GETFL命令获取文件描述符的标志
	int flags = fcntl(fd, F_GETFL, 0);
	//将文件描述符设置为非阻塞状态
	flags |= O_NONBLOCK;
	//如果设置失败，返回-1
	if (-1 == fcntl(fd, F_SETFL, flags)) {
		mytimer_err("SetNonBlock fcntl failed\r\n");
		return -1;
	}
	return 0;
}
/// <summary>
/// 定时器线程
/// </summary>
/// <param name="arg"></param>
/// <returns></returns>
static void* mytimer_loop(void* arg) {
	int res;
	char buff[128];
	//获取参数
	mytimer_handle* __mytimer_handle = (mytimer_handle*)arg;
	if (__mytimer_handle == NULL) {
		mytimer_err("arg is NULL\r\n");
		return NULL;
	}
	if (-1 == __mytimer_handle->mytimer_handle_activate) {
		mytimer_info("thread is done\r\n");
		return NULL;
	}
	//初始化线程锁
	res = pthread_mutex_init(&__mytimer_handle->mytimer_handle_mutex, NULL);
	if (res!=0) {
		mytimer_err("pthread create failed\r\n");
		return NULL;
	}
	//
	//mytimer_add(__mytimer_handle, &_InitTime);
	//开始主循环
	for (; __mytimer_handle->mytimer_handle_activate;) {
		struct epoll_event myevent[MAXNUM];
		//等待timerfd的ready状态 将ready状态下的timerfd信息复制到myevent中
		//TODO: 阻塞状态下工作线程将无法正确退出，需要一个通知事件保证线程正常退出
		int myfds = epoll_wait(__mytimer_handle->mytimer_handle_epoll_fd, myevent, MAXNUM, -1);
		//对ready状态下的timerfd进行处理
		for (int i = 0; i < myfds; i++) {
			mytimer* _mytimer = NULL;
			mytimer_callback _cb = NULL;
			pthread_mutex_lock(&__mytimer_handle->mytimer_handle_mutex);
			//找到对应timerfd的mytimer结构体
			HASH_FIND_INT(__mytimer_handle->mytimer_handle_node, &myevent[i].data.fd, _mytimer);
			if (_mytimer == NULL) {
				mytimer_err("_mytimer is NULL\r\n");
				pthread_mutex_unlock(&__mytimer_handle->mytimer_handle_mutex);
				return NULL;
			}
			pthread_mutex_unlock(&__mytimer_handle->mytimer_handle_mutex);
			//检查是否为可读状态
			while (read(myevent[i].data.fd, buff, 8) > 0);
			//获取回调函数
			_cb = _mytimer->mytimer_cb;
			if (0 == __mytimer_handle->mytimer_handle_activate) {
				mytimer_info("thread is done\r\n");
				break;
			}
			//执行回调函数
			if (_cb && _mytimer->mytimer_count != 0) {
				_cb(_mytimer->mytimer_data);
				if (_mytimer->mytimer_count > 0) {
					_mytimer->mytimer_count--;
				}
			}
			else {
				mytimer_info("callback is null\r\n");
			}
			//判断定时任务次数是否完成
			if (_mytimer->mytimer_count == 0) {
				//epoll中删除当前timerfd的监听
				epoll_ctl(__mytimer_handle->mytimer_handle_epoll_fd, EPOLL_CTL_DEL, _mytimer->mytimer_fd, &myevent[i]);
				//从哈希表中删除当前的任务
				pthread_mutex_lock(&__mytimer_handle->mytimer_handle_mutex);
				HASH_DEL(__mytimer_handle->mytimer_handle_node, _mytimer);
				pthread_mutex_unlock(&__mytimer_handle->mytimer_handle_mutex);
				close(_mytimer->mytimer_fd);
				free(_mytimer->mytimer_data);
				free(_mytimer);
			}
		}
	}
	//关闭epoll
	close(__mytimer_handle->mytimer_handle_epoll_fd);
	//开始进行线程退出前的准备
	pthread_mutex_lock(&__mytimer_handle->mytimer_handle_mutex);
	__mytimer_handle->mytimer_handle_epoll_fd = -1;
	//删除所有timerfd
	for (int i = 0; i <= __mytimer_handle->mytimer_handle_fd_max; i++) {
		mytimer* temp = NULL;
		//从哈希表中找到对应的mytimer结构体
		HASH_FIND_INT(__mytimer_handle->mytimer_handle_node, &i, temp);
		if (temp != NULL) {
			HASH_DEL(__mytimer_handle->mytimer_handle_node, temp);
			close(temp->mytimer_fd);
			if (temp->mytimer_count == -99) {
				continue;
			}
			else {
				free(temp->mytimer_data);
				free(temp);
			}
		}
	}
	pthread_mutex_unlock(&__mytimer_handle->mytimer_handle_mutex);
	pthread_mutex_destroy(&__mytimer_handle->mytimer_handle_mutex);
	free(__mytimer_handle);
	__mytimer_handle = NULL;
	mytimer_info("time pthread exit\r\n");
	pthread_exit(NULL);
}
/// <summary>
/// 初始化定时器
/// </summary>
/// <param name="max_num"></param>
/// <returns></returns>
mytimer_handle* mytimer_init(int max_num) {
	//分配空间
	mytimer_handle* __mytimer_handle = (mytimer_handle*)malloc(sizeof(mytimer_handle));
	if (__mytimer_handle == NULL) {
		mytimer_err("alloc mytimer_handle failed\r\n");
		goto mytimer_handle_alloc_failed;
	}
	__mytimer_handle->mytimer_handle_activate = -1;
	//创建epoll句柄
	__mytimer_handle->mytimer_handle_epoll_fd = epoll_create(max_num);
	if (__mytimer_handle->mytimer_handle_epoll_fd == -1) {
		mytimer_err("epoll create failed\r\n");
		goto epoll_create_failed;
	}
	//保活timerfd
	int _mytimefd = timerfd_create(CLOCK_REALTIME, 0);
	if (_mytimefd < 0) {
		mytimer_err("timerfd create failed \r\n");
	}
	_InitTime.mytimer_cb = KeepAlive;
	_InitTime.mytimer_fd = _mytimefd;
	SetNonBlock(_InitTime.mytimer_fd);
	//更新最大fd数值
	__mytimer_handle->mytimer_handle_fd_max < _InitTime.mytimer_fd ? __mytimer_handle->mytimer_handle_fd_max = _InitTime.mytimer_fd : __mytimer_handle->mytimer_handle_fd_max;
	mytimer_info("New Timerfd: %d added\r\n", _InitTime.mytimer_fd);
	//添加epoll监听的事件
	struct epoll_event _event;
	_event.data.fd = _InitTime.mytimer_fd;
	_event.events = EPOLLET | EPOLLIN;
	//设置时间
	struct itimerspec _mytimersp;
	_mytimersp.it_value.tv_sec = 0;
	_mytimersp.it_value.tv_nsec = 10;
	_mytimersp.it_interval.tv_sec = _mytimersp.it_value.tv_sec;
	_mytimersp.it_interval.tv_nsec = _mytimersp.it_value.tv_nsec;
	int ret = timerfd_settime(_InitTime.mytimer_fd, 0, &_mytimersp, NULL);
	if (ret == -1) {
		mytimer_err("set time error\r\n");
	}
	mytimer_info("InitTime parameter: %ld sec , %ld nsec\r\n", _mytimersp.it_value.tv_sec, _mytimersp.it_value.tv_nsec);
	//添加到epoll监听中
	ret = epoll_ctl(__mytimer_handle->mytimer_handle_epoll_fd, EPOLL_CTL_ADD, _InitTime.mytimer_fd, &_event);
	if (ret == -1) {
		mytimer_err("epoll_ctl add error\r\n");
	}
	//添加到哈希表
	int mytimer_fd = _InitTime.mytimer_fd;
	mytimer* _intime = &_InitTime;
	HASH_ADD_INT(__mytimer_handle->mytimer_handle_node, mytimer_fd, _intime);

	//创建线程
	__mytimer_handle->mytimer_handle_activate = 1;
	pthread_create(&__mytimer_handle->mytimer_handle_thread_id, NULL, mytimer_loop, (void*)__mytimer_handle);
	return __mytimer_handle;
mytimer_handle_alloc_failed:
	mytimer_info("next\r\n");
epoll_create_failed:
	free(__mytimer_handle);
	__mytimer_handle = NULL;
	return NULL;

}
/// <summary>
/// 添加定时任务
/// </summary>
/// <param name="_mth"></param>
/// <param name="_timer"></param>
/// <returns></returns>
int mytimer_add(mytimer_handle* _mth, mytimer* _timer) {
	int ret;
	//初始化定时器
	mytimer* _mytime = (mytimer*)malloc(sizeof(mytimer));
	_mytime->mytimer_count = _timer->mytimer_count;
	_mytime->mytimer_data = _timer->mytimer_data;
	_mytime->mytimer_cb = _timer->mytimer_cb;
	_mytime->mytimer_interval = _timer->mytimer_interval;
	//创建timerfd
	int _mytimefd = timerfd_create(CLOCK_REALTIME, 0);
	if (_mytimefd < 0) {
		mytimer_err("timerfd create failed \r\n");
		goto timerfd_create_faild;
	}
	_mytime->mytimer_fd = _timer->mytimer_fd = _mytimefd;
	SetNonBlock(_mytime->mytimer_fd);
	//更新最大fd数值
	_mth->mytimer_handle_fd_max < _mytime->mytimer_fd ? _mth->mytimer_handle_fd_max = _mytime->mytimer_fd : _mth->mytimer_handle_fd_max;
	mytimer_info("New Timerfd: %d added\r\n",_mytime->mytimer_fd);
	//添加epoll监听的事件
	struct epoll_event _event;
	_event.data.fd = _mytime->mytimer_fd;
	_event.events = EPOLLET | EPOLLIN;
	//设置时间
	struct itimerspec _mytimersp;
	_mytimersp.it_value.tv_sec = (int)_mytime->mytimer_interval;
	_mytimersp.it_value.tv_nsec = (_mytime->mytimer_interval - (int)_mytime->mytimer_interval) * 1000000000;
	if (_mytime->mytimer_count != 0) {
		_mytimersp.it_interval.tv_sec = _mytimersp.it_value.tv_sec;
		_mytimersp.it_interval.tv_nsec = _mytimersp.it_value.tv_nsec;
		ret = timerfd_settime(_mytime->mytimer_fd, 0, &_mytimersp, NULL);
		if (ret == -1) {
			mytimer_err("set time error\r\n");
			goto timerfd_create_faild;
		}
		mytimer_info("Time parameter: %ld sec , %ld nsec\r\n", _mytimersp.it_value.tv_sec, _mytimersp.it_value.tv_nsec);
	}
	//添加到epoll监听中
	ret = epoll_ctl(_mth->mytimer_handle_epoll_fd, EPOLL_CTL_ADD, _mytime->mytimer_fd, &_event);
	if (ret == -1) {
		mytimer_err("epoll_ctl add error\r\n");
		goto timerfd_create_faild;
	}
	//添加到哈希表
	pthread_mutex_lock(&_mth->mytimer_handle_mutex);
	int mytimer_fd = _mytime->mytimer_fd;
	HASH_ADD_INT(_mth->mytimer_handle_node, mytimer_fd, _mytime);
	pthread_mutex_unlock(&_mth->mytimer_handle_mutex);
	//free(_mytime);
	return 0;
timerfd_create_faild:
	free(_mytime);
	return -1;
}
/// <summary>
/// 删除定时任务
/// </summary>
/// <param name="_mth"></param>
/// <param name="_timer"></param>
/// <returns></returns>
int mytimer_del(mytimer_handle* _mth, mytimer* _timer) {
	//从哈希表中删除
	mytimer* temp = NULL;
	pthread_mutex_lock(&_mth->mytimer_handle_mutex);
	HASH_FIND_INT(_mth->mytimer_handle_node, &_timer->mytimer_fd, temp);
	if (temp != NULL) {
		mytimer_info("delete Timer fd: %d\r\n", _timer->mytimer_fd);
		struct epoll_event _event;
		_event.data.fd = _timer->mytimer_fd;
		_event.events = EPOLLIN | EPOLLET;
		epoll_ctl(_mth->mytimer_handle_epoll_fd, EPOLL_CTL_DEL, _timer->mytimer_fd, &_event);
		close(_timer->mytimer_fd);
		HASH_DEL(_mth->mytimer_handle_node, temp);
		free(temp->mytimer_data);
		free(temp);
	}
	pthread_mutex_unlock(&_mth->mytimer_handle_mutex);
	mytimer_info("hash map count :%d\r\n", HASH_COUNT(_mth->mytimer_handle_node));
	return 0;
}
/// <summary>
/// 销毁定时器线程
/// </summary>
/// <param name="_mth"></param>
/// <returns></returns>
int mytimer_des(mytimer_handle* _mth) {
	//结束线程
	_mth->mytimer_handle_activate = 0;
	pthread_join(_mth->mytimer_handle_thread_id, NULL);
	return 0;
}
/// <summary>
/// 获取定时任务的数量
/// </summary>
/// <param name="_mth"></param>
/// <returns></returns>
int mytimer_count(mytimer_handle* _mth) {
	return HASH_COUNT(_mth->mytimer_handle_node) - 1;
}