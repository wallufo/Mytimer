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
/// ��������
/// </summary>
/// <param name="arg"></param>
/// <returns></returns>
static int KeepAlive(void* arg) {
	mytimer_info("keep alive\r\n");
	return 0;
}
/// <summary>
/// ����fdΪ������״̬
/// </summary>
/// <param name="fd"></param>
/// <returns></returns>
static int SetNonBlock(int fd)
{
	//ͨ��fcntl��F_GETFL�����ȡ�ļ��������ı�־
	int flags = fcntl(fd, F_GETFL, 0);
	//���ļ�����������Ϊ������״̬
	flags |= O_NONBLOCK;
	//�������ʧ�ܣ�����-1
	if (-1 == fcntl(fd, F_SETFL, flags)) {
		mytimer_err("SetNonBlock fcntl failed\r\n");
		return -1;
	}
	return 0;
}
/// <summary>
/// ��ʱ���߳�
/// </summary>
/// <param name="arg"></param>
/// <returns></returns>
static void* mytimer_loop(void* arg) {
	int res;
	char buff[128];
	//��ȡ����
	mytimer_handle* __mytimer_handle = (mytimer_handle*)arg;
	if (__mytimer_handle == NULL) {
		mytimer_err("arg is NULL\r\n");
		return NULL;
	}
	if (-1 == __mytimer_handle->mytimer_handle_activate) {
		mytimer_info("thread is done\r\n");
		return NULL;
	}
	//��ʼ���߳���
	res = pthread_mutex_init(&__mytimer_handle->mytimer_handle_mutex, NULL);
	if (res!=0) {
		mytimer_err("pthread create failed\r\n");
		return NULL;
	}
	//
	//mytimer_add(__mytimer_handle, &_InitTime);
	//��ʼ��ѭ��
	for (; __mytimer_handle->mytimer_handle_activate;) {
		struct epoll_event myevent[MAXNUM];
		//�ȴ�timerfd��ready״̬ ��ready״̬�µ�timerfd��Ϣ���Ƶ�myevent��
		//TODO: ����״̬�¹����߳̽��޷���ȷ�˳�����Ҫһ��֪ͨ�¼���֤�߳������˳�
		int myfds = epoll_wait(__mytimer_handle->mytimer_handle_epoll_fd, myevent, MAXNUM, -1);
		//��ready״̬�µ�timerfd���д���
		for (int i = 0; i < myfds; i++) {
			mytimer* _mytimer = NULL;
			mytimer_callback _cb = NULL;
			pthread_mutex_lock(&__mytimer_handle->mytimer_handle_mutex);
			//�ҵ���Ӧtimerfd��mytimer�ṹ��
			HASH_FIND_INT(__mytimer_handle->mytimer_handle_node, &myevent[i].data.fd, _mytimer);
			if (_mytimer == NULL) {
				mytimer_err("_mytimer is NULL\r\n");
				pthread_mutex_unlock(&__mytimer_handle->mytimer_handle_mutex);
				return NULL;
			}
			pthread_mutex_unlock(&__mytimer_handle->mytimer_handle_mutex);
			//����Ƿ�Ϊ�ɶ�״̬
			while (read(myevent[i].data.fd, buff, 8) > 0);
			//��ȡ�ص�����
			_cb = _mytimer->mytimer_cb;
			if (0 == __mytimer_handle->mytimer_handle_activate) {
				mytimer_info("thread is done\r\n");
				break;
			}
			//ִ�лص�����
			if (_cb && _mytimer->mytimer_count != 0) {
				_cb(_mytimer->mytimer_data);
				if (_mytimer->mytimer_count > 0) {
					_mytimer->mytimer_count--;
				}
			}
			else {
				mytimer_info("callback is null\r\n");
			}
			//�ж϶�ʱ��������Ƿ����
			if (_mytimer->mytimer_count == 0) {
				//epoll��ɾ����ǰtimerfd�ļ���
				epoll_ctl(__mytimer_handle->mytimer_handle_epoll_fd, EPOLL_CTL_DEL, _mytimer->mytimer_fd, &myevent[i]);
				//�ӹ�ϣ����ɾ����ǰ������
				pthread_mutex_lock(&__mytimer_handle->mytimer_handle_mutex);
				HASH_DEL(__mytimer_handle->mytimer_handle_node, _mytimer);
				pthread_mutex_unlock(&__mytimer_handle->mytimer_handle_mutex);
				close(_mytimer->mytimer_fd);
				free(_mytimer->mytimer_data);
				free(_mytimer);
			}
		}
	}
	//�ر�epoll
	close(__mytimer_handle->mytimer_handle_epoll_fd);
	//��ʼ�����߳��˳�ǰ��׼��
	pthread_mutex_lock(&__mytimer_handle->mytimer_handle_mutex);
	__mytimer_handle->mytimer_handle_epoll_fd = -1;
	//ɾ������timerfd
	for (int i = 0; i <= __mytimer_handle->mytimer_handle_fd_max; i++) {
		mytimer* temp = NULL;
		//�ӹ�ϣ�����ҵ���Ӧ��mytimer�ṹ��
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
/// ��ʼ����ʱ��
/// </summary>
/// <param name="max_num"></param>
/// <returns></returns>
mytimer_handle* mytimer_init(int max_num) {
	//����ռ�
	mytimer_handle* __mytimer_handle = (mytimer_handle*)malloc(sizeof(mytimer_handle));
	if (__mytimer_handle == NULL) {
		mytimer_err("alloc mytimer_handle failed\r\n");
		goto mytimer_handle_alloc_failed;
	}
	__mytimer_handle->mytimer_handle_activate = -1;
	//����epoll���
	__mytimer_handle->mytimer_handle_epoll_fd = epoll_create(max_num);
	if (__mytimer_handle->mytimer_handle_epoll_fd == -1) {
		mytimer_err("epoll create failed\r\n");
		goto epoll_create_failed;
	}
	//����timerfd
	int _mytimefd = timerfd_create(CLOCK_REALTIME, 0);
	if (_mytimefd < 0) {
		mytimer_err("timerfd create failed \r\n");
	}
	_InitTime.mytimer_cb = KeepAlive;
	_InitTime.mytimer_fd = _mytimefd;
	SetNonBlock(_InitTime.mytimer_fd);
	//�������fd��ֵ
	__mytimer_handle->mytimer_handle_fd_max < _InitTime.mytimer_fd ? __mytimer_handle->mytimer_handle_fd_max = _InitTime.mytimer_fd : __mytimer_handle->mytimer_handle_fd_max;
	mytimer_info("New Timerfd: %d added\r\n", _InitTime.mytimer_fd);
	//���epoll�������¼�
	struct epoll_event _event;
	_event.data.fd = _InitTime.mytimer_fd;
	_event.events = EPOLLET | EPOLLIN;
	//����ʱ��
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
	//��ӵ�epoll������
	ret = epoll_ctl(__mytimer_handle->mytimer_handle_epoll_fd, EPOLL_CTL_ADD, _InitTime.mytimer_fd, &_event);
	if (ret == -1) {
		mytimer_err("epoll_ctl add error\r\n");
	}
	//��ӵ���ϣ��
	int mytimer_fd = _InitTime.mytimer_fd;
	mytimer* _intime = &_InitTime;
	HASH_ADD_INT(__mytimer_handle->mytimer_handle_node, mytimer_fd, _intime);

	//�����߳�
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
/// ��Ӷ�ʱ����
/// </summary>
/// <param name="_mth"></param>
/// <param name="_timer"></param>
/// <returns></returns>
int mytimer_add(mytimer_handle* _mth, mytimer* _timer) {
	int ret;
	//��ʼ����ʱ��
	mytimer* _mytime = (mytimer*)malloc(sizeof(mytimer));
	_mytime->mytimer_count = _timer->mytimer_count;
	_mytime->mytimer_data = _timer->mytimer_data;
	_mytime->mytimer_cb = _timer->mytimer_cb;
	_mytime->mytimer_interval = _timer->mytimer_interval;
	//����timerfd
	int _mytimefd = timerfd_create(CLOCK_REALTIME, 0);
	if (_mytimefd < 0) {
		mytimer_err("timerfd create failed \r\n");
		goto timerfd_create_faild;
	}
	_mytime->mytimer_fd = _timer->mytimer_fd = _mytimefd;
	SetNonBlock(_mytime->mytimer_fd);
	//�������fd��ֵ
	_mth->mytimer_handle_fd_max < _mytime->mytimer_fd ? _mth->mytimer_handle_fd_max = _mytime->mytimer_fd : _mth->mytimer_handle_fd_max;
	mytimer_info("New Timerfd: %d added\r\n",_mytime->mytimer_fd);
	//���epoll�������¼�
	struct epoll_event _event;
	_event.data.fd = _mytime->mytimer_fd;
	_event.events = EPOLLET | EPOLLIN;
	//����ʱ��
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
	//��ӵ�epoll������
	ret = epoll_ctl(_mth->mytimer_handle_epoll_fd, EPOLL_CTL_ADD, _mytime->mytimer_fd, &_event);
	if (ret == -1) {
		mytimer_err("epoll_ctl add error\r\n");
		goto timerfd_create_faild;
	}
	//��ӵ���ϣ��
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
/// ɾ����ʱ����
/// </summary>
/// <param name="_mth"></param>
/// <param name="_timer"></param>
/// <returns></returns>
int mytimer_del(mytimer_handle* _mth, mytimer* _timer) {
	//�ӹ�ϣ����ɾ��
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
/// ���ٶ�ʱ���߳�
/// </summary>
/// <param name="_mth"></param>
/// <returns></returns>
int mytimer_des(mytimer_handle* _mth) {
	//�����߳�
	_mth->mytimer_handle_activate = 0;
	pthread_join(_mth->mytimer_handle_thread_id, NULL);
	return 0;
}
/// <summary>
/// ��ȡ��ʱ���������
/// </summary>
/// <param name="_mth"></param>
/// <returns></returns>
int mytimer_count(mytimer_handle* _mth) {
	return HASH_COUNT(_mth->mytimer_handle_node) - 1;
}