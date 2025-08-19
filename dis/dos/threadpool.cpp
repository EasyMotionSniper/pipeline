#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>
#include <atomic>

class ThreadPool {
public:
    explicit ThreadPool(size_t threads = std::thread::hardware_concurrency());
    ~ThreadPool();

    // 提交任务并返回future
    template<class F, class... Args>
    auto enqueueFuture(F&& f, Args&&... args) 
        -> std::future<typename std::result_of<F(Args...)>::type>;

    // 获取当前工作线程数量
    size_t size() const { return workers.size(); }
    
    // 获取队列中待处理任务数量
    size_t pendingTasks() const {
        std::unique_lock<std::mutex> lock(queueMutex);
        return tasks.size();
    }

    // 等待所有任务完成
    void waitForAll();
    
    // 停止线程池
    void shutdown();

private:
    // 工作线程
    std::vector<std::thread> workers;
    
    // 任务队列
    std::queue<std::function<void()>> tasks;
    
    // 同步原语
    mutable std::mutex queueMutex;
    std::condition_variable condition;
    std::condition_variable finishedCondition;
    
    // 控制标志
    std::atomic<bool> stop;
    std::atomic<size_t> activeTasks;
};

// 构造函数实现
inline ThreadPool::ThreadPool(size_t threads)
    : stop(false), activeTasks(0) {
    
    if (threads == 0) {
        throw std::invalid_argument("Thread count must be greater than 0");
    }
    
    workers.reserve(threads);
    
    for (size_t i = 0; i < threads; ++i) {
        workers.emplace_back([this] {
            while (true) {
                std::function<void()> task;
                
                {
                    std::unique_lock<std::mutex> lock(queueMutex);
                    
                    // 等待任务或停止信号
                    condition.wait(lock, [this] {
                        return stop.load() || !tasks.empty();
                    });
                    
                    if (stop.load() && tasks.empty()) {
                        break;
                    }
                    
                    // 取出任务
                    task = std::move(tasks.front());
                    tasks.pop();
                    activeTasks.fetch_add(1);
                }
                
                // 执行任务
                try {
                    task();
                } catch (...) {
                    // 捕获所有异常，防止线程异常退出
                }
                
                // 任务完成
                activeTasks.fetch_sub(1);
                finishedCondition.notify_all();
            }
        });
    }
}

// 析构函数实现
inline ThreadPool::~ThreadPool() {
    shutdown();
}

// 提交任务实现
template<class F, class... Args>
auto ThreadPool::enqueueFuture(F&& f, Args&&... args) 
    -> std::future<typename std::result_of<F(Args...)>::type> {
    
    using returnType = typename std::result_of<F(Args...)>::type;
    
    // 创建packaged_task
    auto task = std::make_shared<std::packaged_task<returnType()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
    
    std::future<returnType> result = task->get_future();
    
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        
        // 检查线程池是否已停止
        if (stop.load()) {
            throw std::runtime_error("ThreadPool is stopped");
        }
        
        // 添加任务到队列
        tasks.emplace([task]() {
            (*task)();
        });
    }
    
    condition.notify_one();
    return result;
}

// 等待所有任务完成
inline void ThreadPool::waitForAll() {
    std::unique_lock<std::mutex> lock(queueMutex);
    finishedCondition.wait(lock, [this] {
        return tasks.empty() && activeTasks.load() == 0;
    });
}

// 停止线程池
inline void ThreadPool::shutdown() {
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        stop.store(true);
    }
    
    condition.notify_all();
    
    for (std::thread &worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    
    workers.clear();
}

#endif // THREAD_POOL_H



#include <iostream>
#include <chrono>
#include <string>

// 测试函数
int calculate(int a, int b) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return a + b;
}

std::string process_string(const std::string& input) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    return "Processed: " + input;
}

void no_return_task(int id) {
    std::cout << "Task " << id << " executing on thread " 
              << std::this_thread::get_id() << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

int main() {
    // 创建线程池
    ThreadPool pool(4);
    
    std::cout << "Thread pool created with " << pool.size() << " threads\n";
    
    // 提交不同类型的任务
    std::vector<std::future<int>> int_results;
    std::vector<std::future<std::string>> string_results;
    std::vector<std::future<void>> void_results;
    
    // 提交计算任务
    for (int i = 0; i < 5; ++i) {
        int_results.emplace_back(
            pool.enqueue_future(calculate, i, i * 2)
        );
    }
    
    // 提交字符串处理任务
    for (int i = 0; i < 3; ++i) {
        string_results.emplace_back(
            pool.enqueue_future(process_string, "input_" + std::to_string(i))
        );
    }
    
    // 提交无返回值任务
    for (int i = 0; i < 4; ++i) {
        void_results.emplace_back(
            pool.enqueue_future(no_return_task, i)
        );
    }
    
    // 提交lambda任务
    auto lambda_result = pool.enqueue_future([](int x) -> double {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        return x * 3.14;
    }, 10);
    
    std::cout << "All tasks submitted. Pending: " << pool.pending_tasks() << std::endl;
    
    // 获取结果
    std::cout << "\nInteger calculation results:\n";
    for (auto& future : int_results) {
        try {
            int result = future.get();
            std::cout << "Result: " << result << std::endl;
        } catch (const std::exception& e) {
            std::cout << "Exception: " << e.what() << std::endl;
        }
    }
    
    std::cout << "\nString processing results:\n";
    for (auto& future : string_results) {
        std::cout << "Result: " << future.get() << std::endl;
    }
    
    std::cout << "\nLambda result: " << lambda_result.get() << std::endl;
    
    // 等待所有void任务完成
    for (auto& future : void_results) {
        future.get();
    }
    
    // 测试异常处理
    auto exception_task = pool.enqueue_future([]() -> int {
        throw std::runtime_error("Test exception");
        return 42;
    });
    
    try {
        exception_task.get();
    } catch (const std::exception& e) {
        std::cout << "\nCaught exception: " << e.what() << std::endl;
    }
    
    // 等待所有任务完成
    pool.wait_for_all();
    std::cout << "\nAll tasks completed!" << std::endl;
    
    return 0;
}





#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>

class ThreadPool {
public:
    // 构造函数：指定线程数量，默认使用硬件支持的并发数
    explicit ThreadPool(size_t threadCount = std::thread::hardware_concurrency()) {
        if (threadCount == 0) {
            throw std::invalid_argument("Thread count must be greater than 0");
        }
        StartThreads(threadCount);
    }

    // 析构函数：停止所有线程并等待任务完成
    ~ThreadPool() {
        StopThreads();
    }

    // 禁止拷贝构造和赋值
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // 提交任务并返回future，支持任意参数的函数
    template<class F, class... Args>
    auto EnqueueFuture(F&& f, Args&&... args) 
        -> std::future<typename std::result_of<F(Args...)>::type> {
        
        // 确定任务返回类型
        using ReturnType = typename std::result_of<F(Args...)>::type;

        // 包装任务为packaged_task，并用智能指针管理
        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        // 获取与任务关联的future
        std::future<ReturnType> result = task->get_future();

        // 加锁并将任务加入队列
        {
            std::unique_lock<std::mutex> lock(mTaskMutex);

            // 如果线程池已停止，不接受新任务
            if (mIsStopped) {
                throw std::runtime_error("Enqueue on stopped ThreadPool");
            }

            // 将任务包装为无参函数放入队列
            mTasks.emplace([task]() { (*task)(); });
        }

        // 通知一个等待的线程有新任务
        mCondition.notify_one();
        return result;
    }

    // 获取当前线程数量
    size_t GetThreadCount() const {
        return mThreads.size();
    }

    // 获取当前等待的任务数量
    size_t GetPendingTaskCount() {
        std::unique_lock<std::mutex> lock(mTaskMutex);
        return mTasks.size();
    }

private:
    // 工作线程容器
    std::vector<std::thread> mThreads;

    // 任务队列：存储待执行的任务
    std::queue<std::function<void()>> mTasks;

    // 保护任务队列的互斥锁
    std::mutex mTaskMutex;

    // 用于通知线程的条件变量
    std::condition_variable mCondition;

    // 线程池是否已停止的标志
    bool mIsStopped = false;

    // 启动指定数量的工作线程
    void StartThreads(size_t threadCount) {
        for (size_t i = 0; i < threadCount; ++i) {
            mThreads.emplace_back(&ThreadPool::WorkerLoop, this);
        }
    }

    // 停止所有工作线程
    void StopThreads() {
        // 标记线程池为已停止
        {
            std::unique_lock<std::mutex> lock(mTaskMutex);
            mIsStopped = true;
        }

        // 唤醒所有等待的线程
        mCondition.notify_all();

        // 等待所有线程完成
        for (std::thread& thread : mThreads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    // 工作线程的主循环
    void WorkerLoop() {
        while (true) {
            std::function<void()> task;

            // 等待并获取任务
            {
                std::unique_lock<std::mutex> lock(mTaskMutex);
                
                // 等待条件：有任务或线程池已停止
                mCondition.wait(lock, [this]() {
                    return mIsStopped || !mTasks.empty();
                });

                // 如果线程池已停止且任务队列为空，则退出
                if (mIsStopped && mTasks.empty()) {
                    return;
                }

                // 取出任务
                task = std::move(mTasks.front());
                mTasks.pop();
            }

            // 执行任务
            try {
                task();
            } catch (const std::exception& e) {
                // 可以根据需要处理任务执行中的异常
                std::cerr << "Task execution failed: " << e.what() << std::endl;
            }
        }
    }
};



#ifndef GLOBAL_THREAD_POOL_H
#define GLOBAL_THREAD_POOL_H

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>
#include <atomic>

class GlobalThreadPool {
public:
    // 获取单例实例
    static GlobalThreadPool& getInstance() {
        static GlobalThreadPool instance;
        return instance;
    }

    // 删除拷贝构造和赋值操作
    GlobalThreadPool(const GlobalThreadPool&) = delete;
    GlobalThreadPool& operator=(const GlobalThreadPool&) = delete;

    // 提交任务
    template<class F, class... Args>
    auto enqueueFuture(F&& f, Args&&... args) 
        -> std::future<typename std::result_of<F(Args...)>::type>;

    // 工具方法
    size_t size() const { return workers.size(); }
    size_t pendingTasks() const;
    void waitForAll();
    void shutdown();
    
    // 动态调整线程数
    void resize(size_t newSize);

private:
    // 私有构造函数
    explicit GlobalThreadPool(size_t threads = std::thread::hardware_concurrency());
    ~GlobalThreadPool();

    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    
    mutable std::mutex queueMutex;
    std::condition_variable condition;
    std::condition_variable finishedCondition;
    
    std::atomic<bool> stop;
    std::atomic<size_t> activeTasks;
    
    void workerLoop();
};

// 构造函数实现
inline GlobalThreadPool::GlobalThreadPool(size_t threads)
    : stop(false), activeTasks(0) {
    
    if (threads == 0) {
        threads = 1;
    }
    
    workers.reserve(threads);
    
    for (size_t i = 0; i < threads; ++i) {
        workers.emplace_back(&GlobalThreadPool::workerLoop, this);
    }
}

inline GlobalThreadPool::~GlobalThreadPool() {
    shutdown();
}

inline void GlobalThreadPool::workerLoop() {
    while (true) {
        std::function<void()> task;
        
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            
            condition.wait(lock, [this] {
                return stop.load() || !tasks.empty();
            });
            
            if (stop.load() && tasks.empty()) {
                break;
            }
            
            task = std::move(tasks.front());
            tasks.pop();
            activeTasks.fetch_add(1);
        }
        
        try {
            task();
        } catch (...) {
            // 捕获异常防止线程退出
        }
        
        activeTasks.fetch_sub(1);
        finishedCondition.notify_all();
    }
}

template<class F, class... Args>
auto GlobalThreadPool::enqueueFuture(F&& f, Args&&... args) 
    -> std::future<typename std::result_of<F(Args...)>::type> {
    
    using returnType = typename std::result_of<F(Args...)>::type;
    
    auto task = std::make_shared<std::packaged_task<returnType()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
    
    std::future<returnType> result = task->get_future();
    
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        
        if (stop.load()) {
            throw std::runtime_error("GlobalThreadPool is stopped");
        }
        
        tasks.emplace([task]() {
            (*task)();
        });
    }
    
    condition.notify_one();
    return result;
}

inline size_t GlobalThreadPool::pendingTasks() const {
    std::unique_lock<std::mutex> lock(queueMutex);
    return tasks.size();
}

inline void GlobalThreadPool::waitForAll() {
    std::unique_lock<std::mutex> lock(queueMutex);
    finishedCondition.wait(lock, [this] {
        return tasks.empty() && activeTasks.load() == 0;
    });
}

inline void GlobalThreadPool::shutdown() {
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        stop.store(true);
    }
    
    condition.notify_all();
    
    for (std::thread& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    
    workers.clear();
}

inline void GlobalThreadPool::resize(size_t newSize) {
    if (newSize == 0) {
        newSize = 1;
    }
    
    std::unique_lock<std::mutex> lock(queueMutex);
    
    size_t currentSize = workers.size();
    
    if (newSize > currentSize) {
        // 增加线程
        workers.reserve(newSize);
        for (size_t i = currentSize; i < newSize; ++i) {
            workers.emplace_back(&GlobalThreadPool::workerLoop, this);
        }
    } else if (newSize < currentSize) {
        // 减少线程 - 这里简化实现，实际使用中可能需要更复杂的逻辑
        // 注意：这种实现会重启整个线程池
        bool wasRunning = !stop.load();
        shutdown();
        
        if (wasRunning) {
            stop.store(false);
            workers.reserve(newSize);
            for (size_t i = 0; i < newSize; ++i) {
                workers.emplace_back(&GlobalThreadPool::workerLoop, this);
            }
        }
    }
}

#endif // GLOBAL_THREAD_POOL_H