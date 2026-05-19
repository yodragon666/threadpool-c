#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include "simple_pool.h"

/*
    设置的工作函数，工作线程会执行这个函数
*/
void my_task(void *arg)
{
    int num = *(int *)arg;

    printf("线程:%lu,正在执行任务：%d\n",
        (unsigned long)pthread_self(),num);

    free(arg);
    sleep(1);
}
//一个进程启动后，天然有一个线程再跑main函数
int main()
{
    /*
        创建四个工作线程，从这里之后，整个进程有五个线程在跑
    */
    threadpool_t *pool = pool_create(4);

    /*
        添加十个任务
    */
    for (int i = 0; i < 20; i++)
    {
        int *num = (int *)malloc(sizeof(int));
        *num = i;
        pool_add_task(pool, my_task, num);
        printf("任务%d已添加\n",i);

    }
    /*
        主线程需要等待一会，等到所有的线程都把活干完了，再销毁线程池，然后return 0，进程关闭
    */
    sleep(10);

    /*
        销毁线程池
    */
    pool_destroy(pool);
    return 0;
}