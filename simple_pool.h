#ifndef SIMPLE_POOL_H
#define SIMPLE_POOL_H

//linux线程库，提供create mutex cond等等多线程功能，很多线程相关的东西都在这里
#include <pthread.h>
#include "task_queue.h"

/*
    线程池结构体
    线程+任务队列
*/
typedef struct threadpool {
    /*
        工作线程数组，本质上是一个无符号长整形，
        每一个线程都有一个唯一的ID，也就是线程的身份证号，
        线程的操作全都是基于这个ID，这个数据就是放线程的ID，
    */
    pthread_t *threads;

    //线程数量
    int thread_count;

    //是否关闭线程池，0是正常运行，1是准备关闭
    int stop;

    //任务队列
    task_queue_t *queue;

    
} threadpool_t;

//创建线程池
threadpool_t *pool_create(int n);

//添加任务
void pool_add_task(threadpool_t *pool,task_func func,void *arg);

//销毁线程池
void pool_destroy(threadpool_t *pool);

#endif