#ifndef __SHIOSYLAR_SCHEDULER_H__
#define __SHIOSYLAR_SCHEDULER_H__

// 调度器

/*
每个线程有四个协程相关的私有变量--当前正在运行的协程指针、主协程指针、调度协程指针、调度器对象指针
1. 同一个调度器创建出来的线程池，池中线程所含的调度器对象指针都是创建者对象
2. 在调度器中，任务队列的基本单元是协程，创建出来的线程，不断的向队列取任务
3. 线程池中线程的主协程指针、调度协程指针是同一个，当取出任务后，切到任务协程
   任务执行完切回，继续取任务执行，没有任务则忙等待
4. Scheduler使用了use_caller，则将Scheduler的创建线程也算作线程池中的线程，可以少创建一个
   在该创建者线程中，会分别创建一个主协程，一个调度协程，两者分开了，其他线程中这两者是同一个对象
   主线程中不会自动的执行run函数，因为start中没有将主线程的协程切到该调度协程m_rootFiber中
   只会在stop()函数中，stoppping()函数返回false，调度器尚未停止时，才会切到调度协程m_rootFiber
   运行run，其他情况是不会切到主线程的调度协程m_rootFiber中去运行run函数
   并且m_rootFiber是开启了use_caller，任务完成切到主协程而不是切到调度协程
*/

#include <memory>
#include <vector>
#include <list>
#include <iostream>
#include "fiber.h"
#include "thread.h"

namespace shiosylar
{

// 调度器类
class Scheduler
{
public:
	typedef std::shared_ptr<Scheduler> ptr;
	typedef Mutex MutexType;

	Scheduler(size_t threads = 1, bool use_caller = true, const std::string& name = "");

	virtual ~Scheduler();

	// 获取调度器名称
	const std::string& getName() const { return m_name; }

	// 获取调度器对象的指针
	static Scheduler* GetThis();

	// 获取调度器协程对象的指针
	static Fiber* GetMainFiber();

	// 开启调度器
	void start();

	// 停止调度器
	void stop();

	// 向任务队列插入单个任务，thread参数指定任务执行的线程，-1为无限制
	template<class FiberOrCb>
	void schedule(FiberOrCb fc, int thread = -1)
	{
		bool need_tickle = false;
		{
			MutexType::Lock lock(m_mutex);
			need_tickle = scheduleNoLock(fc, thread);
		}

		if(need_tickle)
			tickle();
	}

	// 批量的向任务队列插入任务
	template<class InputIterator>
	void schedule(InputIterator begin, InputIterator end)
	{
		bool need_tickle = false;
		{
			MutexType::Lock lock(m_mutex);
			while(begin != end)
			{
				need_tickle = scheduleNoLock(&*begin, -1) || need_tickle;
				++begin;
			}
		}
		if(need_tickle)
			tickle();
	}

	void switchTo(int thread = -1);

	std::ostream& dump(std::ostream& os);

protected:
	// 用于通知各个线程有任务到来
	virtual void tickle();

	// 线程的入口函数
	void run();

	// 调度器是否已经停止
	virtual bool stopping();

	// 没有取到任务的忙等待函数
	virtual void idle();

	// 设置当前线程的调度器指针
	void setThis();

	// 查询当前是否有空闲线程
	bool hasIdleThreads()
	{
		return m_idleThreadCount > 0;
	}

private:
	// 向任务队列插入任务，底层封装
	template<class FiberOrCb>
	bool scheduleNoLock(FiberOrCb fc, int thread)
	{
		bool need_tickle = m_fibers.empty();
		FiberAndThread ft(fc, thread);
		if(ft.fiber || ft.cb)
			m_fibers.push_back(ft);
		return need_tickle;
	}

private:
	// 任务节点的封装，一个任务至少含有一个协程或者函数
	struct FiberAndThread
	{
		Fiber::ptr fiber;               // 协程
		std::function<void()> cb;       // 协程执行函数
		int thread;                     // 线程id

		FiberAndThread(Fiber::ptr f, int thr) :fiber(f), thread(thr)
		{

		}

		FiberAndThread(Fiber::ptr* f, int thr) :thread(thr)
		{
			// swap后，f变成空，防止计数不减，导致锁死对象
			fiber.swap(*f); // 也可以用std::move，swap底层也是move
		}

		FiberAndThread(std::function<void()> f, int thr) :cb(f), thread(thr)
		{

		}

		FiberAndThread(std::function<void()>* f, int thr) :thread(thr)
		{
			cb.swap(*f);
		}

		FiberAndThread() :thread(-1)
		{

		}

		void reset()
		{
			fiber = nullptr;
			cb = nullptr;
			thread = -1;
		}

	}; // struct FiberAndThread end

private:
	MutexType m_mutex;                          // Mutex                          
	std::vector<Thread::ptr> m_threads;         // 线程池  
	std::list<FiberAndThread> m_fibers;         // 待执行的协程队列
	Fiber::ptr m_rootFiber;                     // use_caller为true时有效,使用当前线程
	std::string m_name;                         // 协程调度器名称

protected:
	std::vector<int> m_threadIds;                   // 协程下的线程id数组
	size_t m_threadCount = 0;                       // 线程数量
	std::atomic<size_t> m_activeThreadCount = {0};  // 工作线程数量
	std::atomic<size_t> m_idleThreadCount = {0};    // 空闲线程数量
	bool m_stopping = true;                         // 是否正在停止
	bool m_autoStop = false;                        // 是否自动停止
	int m_rootThread = 0;                           // 主线程id(use_caller)

}; // class Scheduler end

class SchedulerSwitcher : public noncopyable
{
public:
	SchedulerSwitcher(Scheduler* target = nullptr);

	~SchedulerSwitcher();

private:
	Scheduler* m_caller;

}; // class SchedulerSwitcher end

} // namespace shiosylar end

#endif