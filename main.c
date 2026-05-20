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
//测试1
void test01()
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
}
typedef struct {
    int id;
    int work_time;
    char desc[32];
} task_info_t;

void timed_task(void *arg)
{
    task_info_t *info = (task_info_t *)arg;
    unsigned long tid = (unsigned long)pthread_self();

    printf("[线程%lu] 开始: %s (预计 %ds)\n", tid, info->desc, info->work_time);

    for (int i = 1; i <= info->work_time; i++) {
        sleep(1);
        printf("[线程%lu] %s 进度 %d/%d\n", tid, info->desc, i, info->work_time);
    }

    printf("[线程%lu] 完成: %s\n", tid, info->desc);
    free(arg);
}

void test02()
{
    printf("\n========== 并发线程池演示 ==========\n");
    printf("创建 4 个工作线程，提交 8 个耗时各异的任务\n");
    printf("观察: 不同线程ID交替出现 = 真正在并发执行\n\n");

    threadpool_t *pool = pool_create(4);

    struct {
        char *name;
        int time;
    } tasks[] = {
        {"下载文件", 3},
        {"图片处理", 1},
        {"数据计算", 5},
        {"日志分析", 2},
        {"视频转码", 6},
        {"邮件发送", 1},
        {"报表生成", 4},
        {"备份任务", 2},
    };

    int n = sizeof(tasks) / sizeof(tasks[0]);
    for (int i = 0; i < n; i++) {
        task_info_t *info = (task_info_t *)malloc(sizeof(task_info_t));
        info->id = i;
        info->work_time = tasks[i].time;
        snprintf(info->desc, sizeof(info->desc), "%s(#%d)", tasks[i].name, i);
        pool_add_task(pool, timed_task, info);
        printf("主线程: 已提交 %s\n", info->desc);
    }

    printf("\n>>> 4个线程并发工作中...\n\n");

    sleep(20);
    pool_destroy(pool);
    printf("\n========== 演示结束 ==========\n");
}

//一个进程启动后，天然有一个线程再跑main函数
int main()
{
    test02();
    return 0;
}