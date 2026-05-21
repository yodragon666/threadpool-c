# simple_pool — 轻量级 C 线程池

一个基于 POSIX 线程（pthreads）的高性能 C 线程池实现，采用**快慢路径分流**架构，支持高并发任务提交与消费。

## 项目简介

从零实现的 C 语言线程池项目，意在深入理解多线程编程、并发控制与性能优化的底层原理。完整实现了生产者-消费者模型，并通过对锁的职责拆分，探索了高并发场景下的性能优化路径。

## 特性

- **快慢路径分流** — 自旋锁（spinlock）保护极短的队列操作（fast path），互斥锁 + 条件变量（mutex + condvar）仅用于线程挂起和唤醒（slow path），避免全局 mutex 串行化
- **轻量自旋锁** — 基于 C11 `atomic_flag` 的 Test-And-Set 自旋锁，临界区极短时比 pthread mutex 快一个数量级
- **C11 原子操作** — `task_count` 使用 `atomic_int / atomic_fetch_add`，无需加锁即可安全读写
- **任务队列独立封装** — 队列 (`task_queue_t`) 与线程池 (`threadpool_t`) 逻辑解耦，方便复用或替换
- **生产-消费模型** — 支持多生产者、多消费者并发；队列满时生产者阻塞，队列空时消费者休眠
- **优雅关闭** — 设置停止标志 → 广播唤醒所有线程 → `pthread_join` 等待全部退出，确保无内存泄漏
- **内置基准测试** — `benchmark.c` 提供空任务与轻量任务的多维度压测，输出吞吐量数据

## 架构概览

```
                    ┌─────────────────────────────┐
                    │      生产者线程 (main)        │
                    │    pool_add_task()            │
                    └──────────┬──────────────────┘
                               │
                               ▼
                    ┌─────────────────────────────┐
                    │     任务队列 task_queue_t     │
                    │  ┌───────────────────────┐  │
                    │  │  spinlock (fast path)  │──│── 操作 head/tail / task_count
                    │  │  mutex    (slow path)  │──│── cond_wait / cond_signal
                    │  │  not_empty / not_full  │  │
                    │  └───────────────────────┘  │
                    └──────────┬──────────────────┘
                               │
          ┌────────────────────┼────────────────────┐
          ▼                    ▼                    ▼
    ┌──────────┐        ┌──────────┐         ┌──────────┐
    │ Worker 1 │        │ Worker 2 │         │ Worker N │
    │ fast ✓   │        │ fast ✓   │         │ fast ✓   │
    │ slow 😴  │        │ slow 😴  │         │ slow 😴  │
    └──────────┘        └──────────┘         └──────────┘
```

## 文件结构

| 文件 | 说明 |
|------|------|
| `simple_pool.h` | 线程池结构体与 API 声明 |
| `simple_pool.c` | 线程池实现：worker 函数、创建、添加任务、销毁 |
| `task_queue.h` | 任务队列结构体与 API 声明 |
| `task_queue.c` | 队列实现：初始化、入队、出队、销毁 |
| `spinlock.h` | 基于 C11 `atomic_flag` 的自旋锁（内联函数） |
| `main.c` | 演示用例：基础测试 + 并发任务演示 |
| `benchmark.c` | 性能基准测试 |
| `app` | 编译好的演示程序 |
| `test_pool` | 编译好的测试程序 |
| `benchmark` | 编译好的基准测试程序 |

## API 参考

### 线程池

```c
// 创建包含 n 个工作线程的线程池
threadpool_t *pool_create(int n);

// 向线程池提交一个任务
void pool_add_task(threadpool_t *pool, task_func func, void *arg);

// 销毁线程池：等待所有任务完成，回收所有资源
void pool_destroy(threadpool_t *pool);
```

### 任务定义

```c
typedef void (*task_func)(void *arg);
```

`arg` 支持任意类型指针，多参数时可自行封装结构体传入。

### 自旋锁（内联）

```c
spin_init(&lock);      // 初始化（解锁状态）
spin_lock(&lock);      // 加锁（TAS 自旋）
spin_unlock(&lock);    // 解锁
```

## 核心设计

### 线程池模型

线程池本质是一组预先创建好的工作线程 + 一个共享的任务队列。生产者（主线程或任意线程）向队列提交任务，消费者（工作线程）从队列取出任务并发执行。线程池避免了频繁创建/销毁线程的开销，控制了系统并发度，防止资源耗尽。

### 两锁职责分离

初始版本使用单一的 `pthread_mutex_t` 保护所有队列操作，导致 worker 线程取任务时全部串行化。优化后的版本将锁职责拆分为：

| 组件 | 职责 | 持锁时间 |
|------|------|----------|
| `spinlock` | 保护队列 head/tail 指针 | 纳秒级 |
| `mutex` | 保护 cond_wait / cond_signal 线程调度 | 微秒级 |
| `atomic_int` | 保护 task_count | 完全无锁 |

worker 函数分为 fast path 和 slow path：

```
worker():
  while(1):
    if task_count > 0:          # fast path：有任务直接取，spinlock 快速仲裁
      task = queue_pop(queue)
      task->func()              # 执行任务
      continue
    mutex_lock()                # slow path：没任务了才加 mutex 进入睡眠
    while task_count == 0 && !stop:
      cond_wait()
    if stop && task_count == 0:
      break
    mutex_unlock()
```

### 自旋锁实现

`spinlock.h` 基于 C11 的 `atomic_flag` 实现了一个 Test-And-Set 自旋锁：

```c
struct spinlock {
    atomic_flag lock;  // 0 = 未锁, 1 = 已锁
};
```

- **初始化**：`atomic_flag_clear` 设 `lock = 0`
- **加锁**：`atomic_flag_test_and_set` 返回旧值并设为 1；若旧值为 1（已被持锁），`while` 循环空转等待
- **解锁**：`atomic_flag_clear` 设 `lock = 0`

自旋锁适用于**临界区极短**的场景（如操作队列指针），因为等待者不会休眠，避免了上下文切换开销。但如果临界区较长，自旋锁会浪费 CPU。

### 经典生产-消费者问题

项目中的 `cond_wait` / `cond_signal` 用法严格遵循了 POSIX 条件变量的最佳实践：

- 必须与 mutex 配对使用
- `pthread_cond_wait` 必须在 while 循环中调用（而非 if），以处理**虚假唤醒**（spurious wakeup）
- 队列空时消费者等待 `not_empty`，队列满时生产者等待 `not_full`

即使没有线程调用 `signal`/`broadcast`，`pthread_cond_wait` 也可能返回。因此条件检查必须包裹在 `while` 中，唤醒后重新判断条件是否真正满足。

## 演示程序

### test01 — 基础功能测试

创建 4 个工作线程，提交 20 个简单任务，每个任务打印自己的编号后 sleep(1)。

### test02 — 并发任务演示

创建 4 个工作线程，提交 8 个耗时各异（1s ~ 6s）的模拟任务（如下载文件、图片处理、视频转码等），观察不同线程 ID 交替出现，展示真实的并发执行效果。

## 基准测试

`benchmark.c` 测试三种场景：

1. **空任务** — 仅一次原子自增，测量线程池纯调度开销
2. **空任务 × 4 生产者** — 多线程并发提交，测量锁争抢下的性能
3. **轻量任务** — 100 次整数运算 + 原子自增，测量任务有耗时下的加速比

各场景遍历 1/2/4/8 个 worker 分别输出。

## 编译与运行

项目使用 C11 标准 + pthreads：

```bash
# 编译演示程序
gcc -pthread -std=c11 -O2 -o app main.c simple_pool.c task_queue.c

# 编译基准测试
gcc -pthread -std=c11 -O2 -o benchmark benchmark.c simple_pool.c task_queue.c

# 运行演示
./app

# 运行基准测试
./benchmark
```

## 应用场景

- 高并发异步任务处理（如 Web 服务器请求处理）
- 批量数据处理（日志分析、文件处理、图像处理管线）
- 任何需要控制并发度、避免频繁创建线程的场景

## 学习路径建议

1. 阅读 `spinlock.h` — 理解 TAS 自旋锁与 C11 原子操作
2. 阅读 `task_queue.c` — 理解 spinlock 保护的队列指针操作
3. 阅读 `simple_pool.c` 中的 `worker()` — 理解 fast/slow path 分流设计
4. 阅读 `pool_destroy()` — 理解线程池的优雅关闭流程
5. 运行 `./app` 观察 test02 的并发输出，再运行 `./benchmark` 观察吞吐量数据

## 依赖

- C11 编译器（`stdatomic.h`）
- POSIX 线程库（`pthreads`）

## 许可

MIT
