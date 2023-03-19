#include "../include/scheduler.h"
#include "../include/logger.h"
#include "../include/macro.h"
#include "../include/hook.h"

namespace shiosylar
{

// 全局日志类，名称'system'
static shiosylar::Logger::ptr g_logger = LOG_NAME("system");
// 线程私有变量，调度器对象的指针
static thread_local Scheduler* t_scheduler = nullptr;
// 线程私有变量，调度器协程对象的指针
static thread_local Fiber* t_scheduler_fiber = nullptr;

Scheduler::Scheduler(size_t threads, bool use_caller, const std::string& name)
	:
	m_name(name)
{
	ASSERT(threads > 0);

	if(use_caller)
	{
		shiosylar::Fiber::GetThis(); // 创建主协程
		--threads; // 使用当前线程所以可以少创建一个线程

		ASSERT(GetThis() == nullptr); // 断言当前没有创建调度器对象
		t_scheduler = this; // 调度器指针赋值

		// 初始化调度协程
		m_rootFiber.reset(new Fiber(std::bind(&Scheduler::run, this), 0, true));
		shiosylar::Thread::SetName(m_name); // 设置线程名称和调度器相同的名称

		t_scheduler_fiber = m_rootFiber.get();
		m_rootThread = shiosylar::GetThreadId(); // 设置当前为主线程
		m_threadIds.push_back(m_rootThread);
	}
	else
		m_rootThread = -1;
	m_threadCount = threads;
}

Scheduler::~Scheduler()
{
	ASSERT(m_stopping); // 断言调度器已经停止
	if(GetThis() == this)
		t_scheduler = nullptr;
}

// 获取调度器对象的指针
Scheduler* Scheduler::GetThis()
{
	return t_scheduler;
}

// 获取调度器协程对象的指针
Fiber* Scheduler::GetMainFiber()
{
	return t_scheduler_fiber;
}

// 开启调度器，m_rootFiber协程并不会自动运行run，在stop中才运行
void Scheduler::start()
{
	MutexType::Lock lock(m_mutex);
	if(!m_stopping)
		return;
	m_stopping = false;
	ASSERT(m_threads.empty());

	m_threads.resize(m_threadCount);
	for(size_t i = 0; i < m_threadCount; ++i)
	{
		m_threads[i].reset(new Thread(std::bind(&Scheduler::run, this),
							m_name + "_" + std::to_string(i)));
		m_threadIds.push_back(m_threads[i]->getId());
	}
	lock.unlock();
}

// 停止调度器
void Scheduler::stop()
{
	m_autoStop = true;

	// 如果使用了当前线程且只创建一个线程
	if(m_rootFiber
			&& m_threadCount == 0
			&& (m_rootFiber->getState() == Fiber::TERM
			|| m_rootFiber->getState() == Fiber::INIT))
	{
		LOG_INFO(g_logger) << this << " stopped";
		m_stopping = true;

		if(stopping())
			return;
	}

	if(m_rootThread != -1)
	{
		ASSERT(GetThis() == this); // 断言当前线程在所属的线程池中
	}
	else
		ASSERT(GetThis() != this); // m_rootThread=-1时主线程是没有调度器对象指针的
								   // 断言在Scheduler对象的创建者线程中

	m_stopping = true;

	// 一个一个通知线程
	for(size_t i = 0; i < m_threadCount; ++i)
		tickle();

	if(m_rootFiber)
		tickle();

	// 如果只开一个线程且启用了use_caller，那么主线程不会运行run函数，run函数在m_rootFiber协程中
	// 在停止前，切到m_rootFiber协程中运行run函数，执行任务
	if(m_rootFiber)
	{
		if(!stopping())
			m_rootFiber->call(); // 切到m_rootFiber协程，运行run函数
	}
	// 此处任务都完成了，切回来了
	std::vector<Thread::ptr> thrs;
	{
		MutexType::Lock lock(m_mutex);
		thrs.swap(m_threads); // 释放掉容器里的Thread智能指针
	}

	for(auto& i : thrs)
		i->join();
}

// 设置当前线程的调度器指针
void Scheduler::setThis()
{
	t_scheduler = this;
}

// 线程入口函数，不断从任务队列取任务执行
void Scheduler::run()
{
	LOG_DEBUG(g_logger) << m_name << " run";

	set_hook_enable(true);

	setThis(); // 设置其他线程的Scheduler对象指针为当前对象

	// 在其他的线程创建主协程并设置主协程指针
	if(shiosylar::GetThreadId() != m_rootThread)
		t_scheduler_fiber = Fiber::GetThis().get();

	// 如果没有任务则执行这个协程进行忙等待
	Fiber::ptr idle_fiber(new Fiber(std::bind(&Scheduler::idle, this)));
	Fiber::ptr cb_fiber; // 用来存储任务函数

	FiberAndThread ft;
	while(true) // 大循环，不断的取任务执行
	{
		ft.reset();
		bool tickle_me = false;
		bool is_active = false;
		{
			MutexType::Lock lock(m_mutex);
			auto it = m_fibers.begin();
			while(it != m_fibers.end())
			{
				// 如果该任务设置了必须特定线程执行，则跳过它
				if(it->thread != -1 && it->thread != shiosylar::GetThreadId())
				{
					++it;
					tickle_me = true;
					continue;
				}
				ASSERT(it->fiber || it->cb); // 要么时协程任务，要么函数任务

				// 如果该协程任务正在被执行则跳过它
				if(it->fiber && it->fiber->getState() == Fiber::EXEC)
				{
					++it;
					continue;
				}

				ft = *it;
				m_fibers.erase(it++); // 取出任务，从任务列表中删除

				// 一取出就使活跃线程数加一，防止此时任务队列为空，活跃数也为0
				// 导致stopping返回true，此时还有任务运行，不能返回true，如idle()
				++m_activeThreadCount;
				is_active = true; // 标识成功取到任务
				break;
			}
			tickle_me |= it != m_fibers.end(); // 设置是否唤醒其他线程
		}

		if(tickle_me)
			tickle();

		// 如果任务是协程，则判断协程不在结束态和异常态
		if(ft.fiber && (ft.fiber->getState() != Fiber::TERM
						&& ft.fiber->getState() != Fiber::EXCEPT))
		{
			ft.fiber->swapIn(); // 切入该协程，运行任务
			--m_activeThreadCount; // 这里切回来了，工作结束了，工作线程数减一

			// 如果该协程处于就绪态，则需要重新插入到任务队列
			if(ft.fiber->getState() == Fiber::READY)
				schedule(ft.fiber);
			else if(ft.fiber->getState() != Fiber::TERM
					&& ft.fiber->getState() != Fiber::EXCEPT)
			{
				ft.fiber->m_state = Fiber::HOLD; // 设置为暂停态
			}
			ft.reset(); // 任务已经执行完，释放析构掉该协程对象
		}
		else if(ft.cb) // 如果任务是一个函数，则通过该函数创建一个协程来执行它
		{
			if(cb_fiber)
				cb_fiber->reset(ft.cb); // cb_fiber已创建，传入函数并运行
			else // 未创建，则通过这个函数创建cb_fiber对象
				cb_fiber.reset(new Fiber(ft.cb));
			ft.reset();
			cb_fiber->swapIn(); // 切入到函数协程，运行它
			--m_activeThreadCount; // 切换回来，工作线程数减一
			if(cb_fiber->getState() == Fiber::READY) // 为就绪态，则重新插入任务队列
			{
				schedule(cb_fiber);
				// 任务未完成，cb_fiber所指的对象要保护起来，不能再使用了，要重新创建
				cb_fiber.reset();
			}
			else if(cb_fiber->getState() == Fiber::EXCEPT
					|| cb_fiber->getState() == Fiber::TERM)
			{
				// 任务完全执行完成，重置该协程的任务函数，保留当前对象，下次传函数进来就行了
				cb_fiber->reset(nullptr);
			}
			else
			{
				cb_fiber->m_state = Fiber::HOLD;
				cb_fiber.reset(); // 设置为暂停态，也不能再用了，要重新创建
			}
		}
		else // 当前尚无任务可执行，忙等待
		{
			if(is_active) // 取到了任务却无法执行，则活跃数减一，继续下次循环
			{
				--m_activeThreadCount;
				continue;
			}
			if(idle_fiber->getState() == Fiber::TERM)
			{
				LOG_INFO(g_logger) << "idle fiber term";
				break; // stopping返回true，调度已停止，跳出大循环
			}

			++m_idleThreadCount;
			idle_fiber->swapIn();
			--m_idleThreadCount;
			if(idle_fiber->getState() != Fiber::TERM
					&& idle_fiber->getState() != Fiber::EXCEPT)
			{
				idle_fiber->m_state = Fiber::HOLD;
			}
		}
	}
}

void Scheduler::tickle()
{
	LOG_INFO(g_logger) << "tickle";
}

bool Scheduler::stopping()
{
	MutexType::Lock lock(m_mutex);
	return m_autoStop 
			&& m_stopping
			&& m_fibers.empty()
			&& m_activeThreadCount == 0;
}

void Scheduler::idle()
{
	LOG_INFO(g_logger) << "idle";
	while(!stopping())
		shiosylar::Fiber::YieldToHold();
}

void Scheduler::switchTo(int thread)
{
	ASSERT(Scheduler::GetThis() != nullptr);
	if(Scheduler::GetThis() == this)
	{
		if(thread == -1 || thread == shiosylar::GetThreadId())
			return;
	}
	schedule(Fiber::GetThis(), thread);
	Fiber::YieldToHold();
}

std::ostream& Scheduler::dump(std::ostream& os)
{
	os << "[Scheduler name=" << m_name
	   << " size=" << m_threadCount
	   << " active_count=" << m_activeThreadCount
	   << " idle_count=" << m_idleThreadCount
	   << " stopping=" << m_stopping
	   << " ]" << std::endl << "    ";
	for(size_t i = 0; i < m_threadIds.size(); ++i)
	{
		if(i)
			os << ", ";
		os << m_threadIds[i];
	}
	return os;
}

SchedulerSwitcher::SchedulerSwitcher(Scheduler* target)
{
	m_caller = Scheduler::GetThis();
	if(target)
		target->switchTo();
}

SchedulerSwitcher::~SchedulerSwitcher()
{
	if(m_caller)
		m_caller->switchTo();
}

} // namespace shiosylar end