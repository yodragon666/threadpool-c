#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <stdatomic.h>

#include "simple_pool.h"

/* 全局计数器：累计已执行的任务数 */
static atomic_long g_done;

/* 获取单调时钟，毫秒精度 */
static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

/* ---------- 测试任务 ---------- */

/* 空任务：只做一次原子自增 */
void empty_task(void *arg) {
    atomic_fetch_add(&g_done, 1);
    (void)arg;
}

/* 轻任务：模拟 100 次整数运算 + 原子自增 */
void light_task(void *arg) {
    volatile int x = 0;
    for (int i = 0; i < 100; i++) x += i;
    atomic_fetch_add(&g_done, 1);
    (void)arg;
    (void)x;
}

/* ---------- 多生产者线程参数 ---------- */
typedef struct {
    threadpool_t *pool;
    void (*task_fn)(void*);
    long count;
} producer_arg_t;

void *producer_thread(void *arg) {
    producer_arg_t *pa = (producer_arg_t *)arg;
    for (long i = 0; i < pa->count; i++) {
        pool_add_task(pa->pool, pa->task_fn, NULL);
    }
    return NULL;
}

/* ---------- 单轮压测 ---------- */
static void run_bench(const char *label, int threads, long tasks,
                      void (*task_fn)(void*), int nproducers) {
    atomic_store(&g_done, 0);

    threadpool_t *pool = pool_create(threads);

    double t0 = now_ms();

    if (nproducers > 1) {
        /* 多生产者：开 nproducers 个线程同时发任务 */
        pthread_t producers[nproducers];
        producer_arg_t args[nproducers];
        long per = tasks / nproducers;
        for (int i = 0; i < nproducers; i++) {
            args[i].pool    = pool;
            args[i].task_fn = task_fn;
            args[i].count   = (i == nproducers - 1) ? tasks - per * i : per;
            pthread_create(&producers[i], NULL, producer_thread, &args[i]);
        }
        for (int i = 0; i < nproducers; i++) {
            pthread_join(producers[i], NULL);
        }
    } else {
        /* 单生产者：主线程串行发任务 */
        for (long i = 0; i < tasks; i++) {
            pool_add_task(pool, task_fn, NULL);
        }
    }
    double t_post = now_ms();

    /* 等待消费者线程把所有任务执行完 */
    while (atomic_load(&g_done) < tasks) {
        sched_yield();
    }
    double t_done = now_ms();

    pool_destroy(pool);

    double post_ms = t_post - t0;
    double exec_ms = t_done - t_post;
    double total_ms = t_done - t0;

    printf("%-16s  workers=%-2d  tasks=%-10ld  "
           "post:%8.1fms  exec:%8.1fms  total:%8.1fms  "
           "thrpt:%10.0f t/s\n",
           label, threads, tasks,
           post_ms, exec_ms, total_ms,
           tasks / (total_ms / 1000.0));
}

int main(void) {
    printf("============ simple_pool benchmark ============\n\n");

    /*
     * 测试 1：空任务（纯原子自增），单生产者
     * 测量线程池的纯调度开销
     */
    printf("--- empty task (atomic fetch_add) ---\n");
    run_bench("empty",      1, 2000000, empty_task, 1);
    run_bench("empty",      2, 2000000, empty_task, 1);
    run_bench("empty",      4, 2000000, empty_task, 1);
    run_bench("empty",      8, 2000000, empty_task, 1);

    /*
     * 测试 2：空任务 + 4 生产者并发提交
     * 测量自旋锁/原子操作在高并发提交下的争抢
     */
    printf("\n--- empty task, 4 producers ---\n");
    run_bench("empty×4p",   4, 2000000, empty_task, 4);

    /*
     * 测试 3：轻量任务（100 次整数运算）
     * 任务本身有耗时，看多 worker 的加速效果
     */
    printf("\n--- light task (100 int ops + atomic) ---\n");
    run_bench("light",      1,  500000, light_task, 1);
    run_bench("light",      2,  500000, light_task, 1);
    run_bench("light",      4,  500000, light_task, 1);
    run_bench("light",      8,  500000, light_task, 1);

    printf("\n===============================================\n");
    return 0;
}
