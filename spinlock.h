#ifndef SPINLOCK_H
#define SPINLOCK_H
/*
    pthread_mutex_lock(&queue->mutex);
    这种锁，本质上是一种睡眠锁，也就是说，线程A拿了锁，这个时候线程B也来了，
    发现锁被线程A拿走了，此时B会进入睡眠，CPU会把B挂起，等待A释放锁，再把B唤醒
    这个过程非常繁琐，很消耗资源，但是任务队列的操作非常快，这个时候使用睡眠锁
    是非常不值得，
    所以要使用自旋锁，当线程B发现锁在线程A手上的时候，会一直循环空转，直到线程A
    释放锁，然后线程B瞬间拿到锁，这种自旋锁适合临界区代码很短，等待时间很短的情况
    比如任务队列
*/
//C11原子操作库  
#include <stdatomic.h>
/*
    自旋锁结构体
    atomic_flag:
    原子标志位
    0没上锁
    1已上锁
*/
typedef struct spinlock {
    //原子标志位
    atomic_flag lock;
}spinlock_t;

/*
    初始化自旋锁
    为什么要使用inline（内联）
    为了更快
    因为像自旋锁这种函数非常小，如果正常调用函数，会产生函数调用开销：参数传递
    压栈，跳转函数，返回恢复现场，对于自旋锁这种函数，这些开销可能比函数本身还大，所以
    使用inline，告诉编译器，这个函数可以考虑直接展开到调用位置，减少调用函数的性能
    开销
*/
static inline void spin_init(spinlock_t *s)
{
    //表示解锁状态
    atomic_flag_clear(&s->lock);
}
/*
    加锁
    test_and_set:
    返回旧值，并且设为true
    如果就职时true
    说明已经有人拿锁了
    就一直while自旋等待
*/
static inline void spin_lock(spinlock_t *s)
{
    /*
        atomic_flag_test_and_set(&s->lock)干了两件事
        第一步返回lock旧值，while会根据这个旧值判断是否进入循环空转
        第二步设置为1，也就是上锁
        所以线程A执行spinlock，原来lock是0，执行之后，while(0)直接不进入循环，然后
        lock为1，线程B此时执行spinlock的话，lock为1，直接while(1)循环空转，
        等线程A释放锁之后，线程B才能拿到锁
    */
    while(atomic_flag_test_and_set(&s->lock))
    {

    }
}
/*
    解锁
*/
static inline void spin_unlock(spinlock_t *s)
{
    //lock为0
    atomic_flag_clear(&s->lock);
}

#endif