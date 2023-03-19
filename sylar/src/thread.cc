#include "../include/thread.h"
// #include "../include/logger.h"
// #include "../include/util.h"

namespace shiosylar
{

// 线程私有变量，记录当前线程类的指针
static thread_local Thread* t_thread = nullptr;
// 线程私有变量，记录当前线程类的名称
static thread_local std::string t_thread_name = "UNKNOW";
// 全局的日志器，名称为 'system'
static shiosylar::Logger::ptr g_logger = LOG_NAME("system");

// 获取当前线程类的指针
Thread* Thread::GetThis()
{
    return t_thread;
}

// 获取当前线程类的名称
const std::string& Thread::GetName()
{
    return t_thread_name;
}

// 设置当前线程类的指针
void Thread::SetName(const std::string& name)
{
    if(name.empty())
        return;
    if(t_thread)
        t_thread->m_name = name;
    t_thread_name = name;
}

Thread::Thread(std::function<void()> cb, const std::string& name)
            :
            m_cb(cb),
            m_name(name)
{
    if(name.empty())
        m_name = "UNKNOW";
    int rt = pthread_create(&m_thread, nullptr, &Thread::run, this);
    if(rt)
    {
        LOG_ERROR(g_logger) << "pthread_create thread fail, rt=" << rt
            << " name=" << name;
        throw std::logic_error("pthread_create error");
    }
    m_semaphore.wait(); // 为了安全起见，创建者线程先阻塞于此
}

Thread::~Thread()
{
    if(m_thread)
        pthread_detach(m_thread);
}

void Thread::join()
{
    if(m_thread) 
    {
        int rt = pthread_join(m_thread, nullptr);
        if(rt)
        {
            LOG_ERROR(g_logger) << "pthread_join thread fail, rt=" << rt
                << " name=" << m_name;
            throw std::logic_error("pthread_join error");
        }
        m_thread = 0;
    }
}

void* Thread::run(void* arg)
{
    Thread* thread = (Thread*)arg;
    t_thread = thread;
    t_thread_name = thread->m_name;
    thread->m_id = shiosylar::GetThreadId();
    pthread_setname_np(pthread_self(), thread->m_name.substr(0, 15).c_str());

    std::function<void()> cb;
    cb.swap(thread->m_cb);

    thread->m_semaphore.notify(); // 到这里线程的初始化完成了，可以唤醒创建者线程了

    cb(); // 执行任务函数
    return 0;
}

} // namespace shioshiosylar end