#ifndef __SHIOSYLAR_MUTEX_H__
#define __SHIOSYLAR_MUTEX_H__

// 互斥量封装
// 信号量-互斥锁-读写锁-自旋锁-原子锁（CAS）

#include <thread>
#include <functional>
#include <memory>
#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <atomic>
#include <list>

#include "noncopyable.h"
// #include "fiber.h"

namespace shiosylar
{

// 信号量
class Semaphore : noncopyable
{
public:
    Semaphore(uint32_t count = 0);

    ~Semaphore();

    void wait(); // 等待信号量，V操作

    void notify(); // 增加信号量，P操作

private:
    sem_t m_semaphore; // 信号量

}; // class Semaphore end

// 通用互斥锁模板-RAII机制
template<class T>
struct ScopedLockImpl
{
public:
    // 构造函数传互斥量的引用，并加锁
    ScopedLockImpl(T& mutex) : m_mutex(mutex)
    {
        m_mutex.lock();
        m_locked = true;
    }

    // 析构函数中解锁
    ~ScopedLockImpl()
    {
        unlock();
    }

    void lock()
    {
        if(!m_locked)
        {
            m_mutex.lock();
            m_locked = true;
        }
    }

    void unlock()
    {
        if(m_locked)
        {
            m_mutex.unlock();
            m_locked = false;
        }
    }
private:
    T& m_mutex;         // 互斥量的引用，不管理它的生命周期
    bool m_locked;      // 是否加锁，标志量

};

// 读锁模板RAII
template<class T>
struct ReadScopedLockImpl
{
public:
    ReadScopedLockImpl(T& mutex) : m_mutex(mutex)
    {
        m_mutex.rdlock();
        m_locked = true;
    }

    ~ReadScopedLockImpl()
    {
        unlock();
    }

    void lock()
    {
        if(!m_locked)
        {
            m_mutex.rdlock();
            m_locked = true;
        }
    }

    void unlock()
    {
        if(m_locked)
        {
            m_mutex.unlock();
            m_locked = false;
        }
    }
private:
    T& m_mutex;
    bool m_locked;

};

// 写锁模板RAII
template<class T>
struct WriteScopedLockImpl
{
public:
    WriteScopedLockImpl(T& mutex) : m_mutex(mutex)
    {
        m_mutex.wrlock();
        m_locked = true;
    }

    ~WriteScopedLockImpl()
    {
        unlock();
    }

    void lock()
    {
        if(!m_locked)
        {
            m_mutex.wrlock();
            m_locked = true;
        }
    }

    void unlock()
    {
        if(m_locked)
        {
            m_mutex.unlock();
            m_locked = false;
        }
    }
private:
    T& m_mutex;
    bool m_locked;

};

// 通用互斥锁
class Mutex : noncopyable
{
public: 
    typedef ScopedLockImpl<Mutex> Lock;

    Mutex()
    {
        pthread_mutex_init(&m_mutex, nullptr);
    }

    ~Mutex()
    {
        pthread_mutex_destroy(&m_mutex);
    }

    void lock()
    {
        pthread_mutex_lock(&m_mutex);
    }

    void unlock()
    {
        pthread_mutex_unlock(&m_mutex);
    }

private:
    pthread_mutex_t m_mutex;

};

// 空锁(用于调试)
class NullMutex : noncopyable
{
public:
    typedef ScopedLockImpl<NullMutex> Lock;

    NullMutex() {}

    ~NullMutex() {}

    void lock() {}

    void unlock() {}

};

// 通用读写锁
class RWMutex : noncopyable
{
public:
    typedef ReadScopedLockImpl<RWMutex> ReadLock;

    typedef WriteScopedLockImpl<RWMutex> WriteLock;

    RWMutex()
    {
        pthread_rwlock_init(&m_lock, nullptr);
    }
    
    ~RWMutex()
    {
        pthread_rwlock_destroy(&m_lock);
    }

    void rdlock()
    {
        pthread_rwlock_rdlock(&m_lock);
    }

    void wrlock()
    {
        pthread_rwlock_wrlock(&m_lock);
    }

    void unlock()
    {
        pthread_rwlock_unlock(&m_lock);
    }

private:
    pthread_rwlock_t m_lock;

};

// 空读写锁(用于调试)
class NullRWMutex : noncopyable
{
public:
    typedef ReadScopedLockImpl<NullMutex> ReadLock;

    typedef WriteScopedLockImpl<NullMutex> WriteLock;

    NullRWMutex() {}
    ~NullRWMutex() {}
    void rdlock() {}
    void wrlock() {}
    void unlock() {}
};

// 自旋锁
class Spinlock : noncopyable {
public:
    typedef ScopedLockImpl<Spinlock> Lock;

    Spinlock()
    {
        pthread_spin_init(&m_mutex, 0);
    }

    ~Spinlock()
    {
        pthread_spin_destroy(&m_mutex);
    }

    void lock()
    {
        pthread_spin_lock(&m_mutex);
    }

    void unlock()
    {
        pthread_spin_unlock(&m_mutex);
    }
private:
    pthread_spinlock_t m_mutex;

};

// 原子锁
class CASLock : noncopyable
{
public:
    typedef ScopedLockImpl<CASLock> Lock;

    CASLock()
    {
        m_mutex.clear();
    }

    ~CASLock()
    {

    }

    void lock()
    {
        while(std::atomic_flag_test_and_set_explicit(&m_mutex, std::memory_order_acquire));
    }

    void unlock()
    {
        std::atomic_flag_clear_explicit(&m_mutex, std::memory_order_release);
    }

private:
    volatile std::atomic_flag m_mutex;

};

// class Scheduler;
// class FiberSemaphore : Noncopyable {
// public:
//     typedef Spinlock MutexType;

//     FiberSemaphore(size_t initial_concurrency = 0);
//     ~FiberSemaphore();

//     bool tryWait();
//     void wait();
//     void notify();

//     size_t getConcurrency() const { return m_concurrency;}
//     void reset() { m_concurrency = 0;}
// private:
//     MutexType m_mutex;
//     std::list<std::pair<Scheduler*, Fiber::ptr> > m_waiters;
//     size_t m_concurrency;
// };



} // namespace shiosylar end

#endif