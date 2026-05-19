#ifndef TASK_QUEUE_H
#define TASK_QUEUE_H

#include <pthread.h>
#include <stdatomic.h>
#include "spinlock.h"
//函数指针这个函数没有返回值，参数是void*，因为线程池不知道要执行什么任务，所以保存函数地址
typedef void (*task_func)(void *);

/*
    任务结构体，代表一个任务，这里的arg参数也可以封装一下，传入很多参数
*/
typedef struct task{
    //任务函数
    task_func func;
    //传给函数的参数，如果是多个参数，可以,把arg封装成为一个结构体然后传进来
    void * arg;
    //指向下一个任务
    struct task *next;
} task_t;
/*
    任务队列结构体，单独封装任务队列。可以给其他模块复用，有利于后期拓展
*/
typedef struct task_queue 
{
    

    //任务队列，头指针，尾指针
    task_t * head;
    task_t * tail;

    /*
        现在锁的职责：
        1.mutex负责线程的睡眠和唤醒，因为cond和mutex是绑定关系
        2.spinlock负责极短时间内的队列操作，比如操作head和tail
        3.atomic原子操作保护task_count
    */
    //互斥锁，多个线程同时操作任务队列，必须加锁
    /*
        多个线程再同时操作任务队列（头指针和尾指针）的时候，如果两个线程同时执行，可能会导致
        数据乱套，所以需要一把锁，本质是pthread_mutex_t，操作mutex和cond是通过函数进行的
    */
    pthread_mutex_t mutex;

    /*
        自旋锁，专门保护任务队列，使用mutex保护任务队列性能太差了
    */
    spinlock_t lock;

    /*
        这个条件变量控制的就是线程什么时候睡觉，什么时候唤醒，
        睡觉pthread_cond_wait(&pool->cond,&pool->mutex);
        唤醒pthread_cond_signal(&pool->cond);
        只有参数匹配了，才能够正常的睡觉和苏醒，也就是说，唤醒只会唤醒被某个条件变量
        控制睡眠的线程 
    */
    //条件变量，队列为空，线程睡觉，有任务的时候唤醒线程,0工作，1睡觉
    pthread_cond_t not_empty;

    //当任务满的时候，生产者直接睡觉
    pthread_cond_t not_full;

    //当前任务数量
    /*
        等到锁职责分层之后，task_count的改变不再收到mutex的保护
        此时这个变量面临高并发场景会出错，必须保护自己线程的安全，使用原子变量
    */
    atomic_int task_count;

    //最大的任务数量
    /*
        为了防止生产的任务数量大于消费的线程数量，导致内存无限暴涨
    */
    int max_tasks;

} task_queue_t;
//队列初始化
void queue_init(task_queue_t *queue,int max_tasks);
//队列销毁
void queue_destory(task_queue_t *queue);
//任务入队
void queue_push(task_queue_t *queue,task_t *task);
//任务出队
task_t *queue_pop(task_queue_t *queue);
#endif