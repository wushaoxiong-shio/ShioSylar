#ifndef __SHIOSYLAR_THREAD_H__
#define __SHIOSYLAR_THREAD_H__

// 线程类

#include "mutex.h"
#include "logger.h"
#include "noncopyable.h"
#include <thread>
#include <pthread.h>
#include <memory>
#include <functional>

namespace shiosylar
{

class Thread : noncopyable
{
public:
    typedef std::shared_ptr<Thread> ptr;

    Thread(std::function<void()> cb, const std::string& name = "");

    ~Thread();

    // 获取线程ID
    pid_t getId() const { return m_id;}

    // 获取线程名称
    const std::string& getName() const { return m_name;}

    // 主线程调用join()
    void join();

    // 获取当前线程类的指针
    static Thread* GetThis();

    // 获取当前线程类的名称
    static const std::string& GetName();

    // 设置当前线程类的指针
    static void SetName(const std::string& name);

private:
    // 线程创建的入口函数
    static void* run(void* arg);

private:
    pid_t m_id = -1;                    // 线程的tid
    pthread_t m_thread = 0;             // 线程的pid
    std::function<void()> m_cb;         // 线程的执行函数
    std::string m_name;                 // 线程名称
    Semaphore m_semaphore;              // 信号量

}; // class Thread end

} // namespace shiosylar end

#endif
