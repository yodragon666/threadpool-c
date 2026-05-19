#include "task_queue.h"
#include "spinlock.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
/*
    初始化任务队列
*/
void queue_init(task_queue_t *queue,int max_tasks)
{
    queue->head = NULL;
    queue->tail = NULL; 
    queue->max_tasks = max_tasks;

    //初始化原子变量
    atomic_init(&queue->task_count,0);
    //初始化锁
    pthread_mutex_init(&queue->mutex,NULL);
    //初始化自旋锁
    spin_init(&queue->lock);
    //初始化条件变量
    pthread_cond_init(&queue->not_empty,NULL);
    pthread_cond_init(&queue->not_full,NULL);

}
/*
    队列销毁
*/
void queue_destory(task_queue_t *queue)
{
    /*
        销毁锁
    */
    pthread_mutex_destroy(&queue->mutex);
    /*
        销毁条件变量
    */
    pthread_cond_destroy(&queue->not_empty);
    pthread_cond_destroy(&queue->not_full);
}
/*
    任务入队函数
*/
void queue_push(task_queue_t *queue,task_t *task)
{
    //加自旋锁
    spin_lock(&queue->lock);
    /*
        如果队列为空
    */
    if(queue->head == NULL)
    {
        queue->head = task;
        queue->tail = task;
    }
    else 
    {
        /*
            尾插法
        */
        queue->tail->next = task;
        queue->tail = task;
    }
    //任务数量增加,原子增加
    atomic_fetch_add(&queue->task_count, 1);
    //解锁
    spin_unlock(&queue->lock);
}

/*
    任务出队函数
*/
task_t *queue_pop(task_queue_t *queue)
{
    //加自旋锁
    spin_lock(&queue->lock);
    /*
        如果队列为空
    */
    if(queue->head == NULL)
    {
        spin_unlock(&queue->lock);
        return NULL;
    }
    /*
        取头结点
    */
    task_t *task = queue->head;
    /*
        head后移
    */
    queue->head = task->next;
    //如果队列空了
    if(queue->head == NULL)
    {
        queue->tail = NULL;
    }
    //任务数量减少,原子减少
    atomic_fetch_sub(&queue->task_count, 1);
    //解锁
    spin_unlock(&queue->lock);

    return task;
}