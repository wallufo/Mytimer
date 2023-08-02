#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include "mytimer.h"

int timer_cb1(void* arg)
{
	int* user_data = (int *)arg;
	printf("call back data:%d\n", *user_data);
	return 0;
}

int timer_cb2(void* arg)
{
	int* user_data = (int*)arg;
	printf("call back data:%d\n", *user_data);
	return 0;
}

int main(int argc, char* argv[])
{
	mytimer_handle* timer_handle = mytimer_init(32);
	if (NULL == timer_handle) {
		fprintf(stderr, "timer_server_init failed\n");
		return -1;
	}
	mytimer timer1;
	timer1.mytimer_count = 3;
	timer1.mytimer_interval = 0.5;
	timer1.mytimer_cb = timer_cb1;
	int* user_data1 = (int*)malloc(sizeof(int));
	*user_data1 = 100;
	timer1.mytimer_data = user_data1;
	mytimer_add(timer_handle, &timer1);

	mytimer timer2;
	timer2.mytimer_count = -1;
	timer2.mytimer_interval = 0.5;
	timer2.mytimer_cb = timer_cb2;
	int* user_data2 = (int*)malloc(sizeof(int));
	*user_data2 = 10;
	timer2.mytimer_data = user_data2;
	mytimer_add(timer_handle, &timer2);
	printf("Timr Count: %d\r\n", mytimer_count(timer_handle));
	sleep(10);
	mytimer_del(timer_handle, &timer2);
	printf("Timr Count: %d\r\n", mytimer_count(timer_handle));
	mytimer_des(timer_handle);
	getchar();

	return 0;
}

