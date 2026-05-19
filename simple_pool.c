
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>


#include "simple_pool.h"
#include "task_queue.h"


/*
    工作队列执行函数
    每个线程创建后，都会执行这个函数，如果有任务的话，线程就会不断在这里跑任务，
    如果没任务就进入休眠状态，直到被唤醒

    线程一旦创建，就会一直在这个函数里面，
    除非退出函数，如果是正常的关闭线程池，
    stop为1并且队列清空，break之后线程结束
    如果程序直接退出，线程也会一直消亡

*/
// void *worker(void *thread_pool)
// {
//     //拿到线程池对象
//     threadpool_t *pool = (threadpool_t *)thread_pool;
//     printf("线程：%lu开始工作\n",(unsigned long)pthread_self());
//     /* 
//         工作线程永远循环
//         不断从任务队列取任务
//     */                 
//     while(1)
//     {
//         //task:从当前线程拿到的任务
//         task_t *task = NULL;
//         //先加锁，因为下面下面要操作任务队列
//         /*
//             这个函数的调用者一定是某一个工作线程，每一个工作线程都可以调用这个函数，但是这一把锁是全局的，
//         */
//         pthread_mutex_lock(&pool->queue->mutex);

//         /*
//             1 这里stop为0的时候，就是线程池正常工作，stop为1的话就是准备睡眠，等待下一个任务到来
//         */
//         /*
//             2 现在不能直接再线程池里面看head了，因为head是受到lock保护的而不是mutex保护
//         */
//         while(atomic_load(&pool->queue->task_count) == 0 && !pool->stop)
//         {
//             /*
//                 注意，再等待的时候一定要使用while循环，因为虽然线程被唤醒了
//                 但是可能是虚假唤醒，一定要再次判断是否满足条件
//                 同时为了防止竞争条件，也就是说，加入有四个线程都在等待not_empty，但是只有
//                 假如只有一个任务，但是四个线程都被唤醒了，第一个线程拿到了任务，后面的
//                 线程操作空指针就会出错
//             */
//             /*
//                 如果进到了这里，说明线程池还在工作，但是已经没有任务了，直接进入睡眠，如果线程执行到这里，就停住了，
//                 CPU不会再执行这个线程的任何代码，包括while循环，只有被唤醒之后，才会恢复执行，再次检查while条件
//             */
//             printf("线程：%lu进入睡眠状态\n",(unsigned long)pthread_self());
//             pthread_cond_wait(&pool->queue->not_empty,&pool->queue->mutex);
//             printf("线程：%lu被唤醒\n",(unsigned long)pthread_self());
            
//         }

  
//         if (pool->stop && atomic_load(&pool->queue->task_count))
//         {
//             /*
//                 如果已经没任务了，并且已经进入了下班状态，直接解锁，然后退出线程
//             */
//             pthread_mutex_unlock(&pool->queue->mutex);
//             break;
//         }

//         //取任务
//         task = queue_pop(pool->queue);
//         /*
//             通知生产者,任务队列不满了
//         */
//         pthread_cond_signal(&pool->queue->not_full);


//         //队列结束操作，解锁
//         pthread_mutex_unlock(&pool->queue->mutex);

//         //执行任务
//         task->func(task->arg);


//         //任务执行结束，释放任务内存
//         free(task);
//     }
    
//     //这里的返回值要和pthread_join里面的第二个参数匹配
//     return NULL;

// }
/*
    原先的worker函数，一把mutex锁关了任务队列还有线程睡眠，性能非常差，多个线程一起工作的时候
    同时只有一个线程取任务，因为mutex串行化，而这个版本，多个线程取任务的时候会同时竞争spinlock
    而spinlock非常轻量化，速度极快

    当前版本采用了快慢路径分流架构，高频快速的抢任务还有低频的挂起睡眠，两把锁的职责很清晰
*/
void *worker(void *thread_pool)
{
    //拿到线程池对象
    threadpool_t *pool = (threadpool_t *)thread_pool;
    printf("线程：%lu开始工作\n",(unsigned long)pthread_self());
    /* 
        工作线程永远循环
        不断从任务队列取任务
    */   
    while(1)
    {
        task_t *task = NULL;
        /*
            fast path
            有任务直接拿
        */
        if(atomic_load(&pool->queue->task_count) > 0)
        {
            task = queue_pop(pool->queue);
            /*
                防止多个线程同时竞争，导致task被抢走
            */
            if(task != NULL)
            {
                /*
                    通知生产者，队列不满了
                */
                pthread_cond_signal(& pool->queue->not_full);
                /*
                    执行任务
                */
                task->func(task->arg);
                free(task);
                //继续下一轮
                continue;
            }
        }
        /*
            slow path 没有任务了才会进入睡眠
        */
        //这个时候才加mutex互斥锁，保证mutex只负责线程池的睡眠
        pthread_mutex_lock(&pool->queue->mutex);
        //没有任务但是没有下班，保持睡眠
        while(atomic_load(&pool->queue->task_count) == 0 && !pool->stop)
        { 
            printf("线程%lu进入睡眠\n",(unsigned long)pthread_self());
            pthread_cond_wait(&pool->queue->not_empty, &pool->queue->mutex);
            printf("线程%lu被唤醒\n",(unsigned long)pthread_self());
        }
        /*
            线程池被关闭，直接解锁，并且退出工作函数
        */
        if(pool->stop && atomic_load(&pool->queue->task_count) == 0)
        {
            pthread_mutex_unlock(&pool->queue->mutex);
            printf("线程%lu停止工作\n",(unsigned long)pthread_self());
            break;
        }
        pthread_mutex_unlock(&pool->queue->mutex);
    } 
    return NULL;             
}

/*
    创建线程池，创建线程的流程
    分配线程池内存->初始化数量->初始化队列->初始化stop
    ->初始化mutex->初始化cond->分配线程数组内存->创建线程
*/
threadpool_t *pool_create(int n)
{
    //分配线程池内存
    threadpool_t *pool = (threadpool_t*)malloc(sizeof(threadpool_t));
    if(pool==NULL)
    {
        printf("线程池创建失败");
        return NULL;
    }

    //初始化线程数量
    pool->thread_count = n;
    //初始化stop，stop在线程池工作状态就是0，如果要销毁线程池，就为1，
    pool->stop = 0;

    /*
        给任务队列分配内存
    */
    pool->queue = (task_queue_t *) malloc(sizeof( task_queue_t));
    //初始化队列
    queue_init(pool->queue,10000);

    /*
        创建线程数组内存
    */
    pool->threads = (pthread_t *)malloc(sizeof(pthread_t) * n);
    /*
        创建工作线程，真正创建线程，在这里创建之后，线程就会永远进入worker函数工作了
    */
    for (int i = 0; i < n; i++)
    {
        pthread_create(&pool->threads[i],NULL,worker,pool);
        printf("第%d号线程已经创建，ID：%lu\n",i,(unsigned long)pool->threads[i]);
    }
    return pool;
}

/*
    添加任务
*/
void pool_add_task(threadpool_t *pool,task_func func,void *arg)
{
    /*
        创建任务节点
    */
    task_t *task = (task_t *)malloc(sizeof(task_t));

    task->func = func;
    task->arg = arg; //如果参数是很多，需要自己封装结构体，然后分别赋值
    task->next = NULL;


    /*
        操作任务队列之前需要加锁
    */
    pthread_mutex_lock(&pool->queue->mutex);
    /*
        检查任务队列是否已经满了,读取task_count必须使用原子读
        一定要使用while循环，去处理等待机制
    */
    while(atomic_load(&pool->queue->task_count) >= pool->queue->max_tasks)
    {
        printf("任务队列已满，生产者线程等待\n");
        /*
            主线程执行到这里，说明任务队列满了，主线程会把锁释放，然后睡觉，等到
            被signal唤醒的时候，会再次拿到锁，但是有一个问题，如果此时锁在另一个线程
            A的手上，此时时刻会怎么办
            主线程并不会把锁直接抢走，而是进入一个锁竞争，等待cpu调度，直到线程A把任务取走，
            把锁交出来，才会重新回到主线程手上，主线程才会苏醒
        */
        pthread_cond_wait(&pool->queue->not_full,&pool->queue->mutex);
        printf("生产者线程苏醒\n");

    }

    //放任务
    queue_push(pool->queue, task);
    //通知消费者
    pthread_cond_signal(&pool->queue->not_empty);

    /*
        解锁    
    */
    pthread_mutex_unlock(&pool->queue->mutex);

}


/*
    销毁线程池   
    销毁流程：
    加锁->stop为1，要下班->唤醒所有线程->解锁->等待线程结束
    ->释放线程数组->销毁mutex->销毁cond->释放线程池对象
*/
void pool_destroy(threadpool_t *pool)
{
    printf("线程池即将销毁\n");
    /*
        加锁，在多线程共享的数据上面读写的时候最好都加锁
    */
    pthread_mutex_lock(&pool->queue->mutex);
    /*
        设置stop
        告诉工作线程
        准备下班
    */
    pool->stop = 1;
    /*
        唤醒所有线程，让正在睡觉的线程退出worker函数，
    */
    pthread_cond_broadcast(&pool->queue->not_empty);
    /*
        解锁
    */
    pthread_mutex_unlock(&pool->queue->mutex);

    /*
        等待所有线程结束
    */
    for (int i = 0; i < pool->thread_count; i++)
    {
        /*
            主线程一个个等待所有线程退出，线程退出需要时间，必须等待所有都退出，要不可能
            有的线程还在跑函数，就直接被free了
            第二个参数是约定的线程返回值，要和worker里面的返回值匹配
        */
        pthread_join(pool->threads[i], NULL);
    }
    /*
        释放所有线程数组，销毁所有线程
    */
    free(pool->threads);

    /* 
        销毁队列
    */
    queue_destory(pool->queue);
    /*
        释放任务队列
    */
    free(pool->queue);
    /*
        释放线程池对象
    */
    free(pool);

}
