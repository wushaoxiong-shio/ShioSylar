#ifndef __SHIOSYLAR_TIMER_H__
#define __SHIOSYLAR_TIMER_H__

// 定时器类的封装，由IOManager继承使用

#include <memory>
#include <vector>
#include <set>
#include "thread.h"

namespace shiosylar
{

class TimerManager;

// 定时器本体类， 继承于智能指针，便于管理
class Timer : public std::enable_shared_from_this<Timer>
{

friend class TimerManager;

public:
	typedef std::shared_ptr<Timer> ptr;

	bool cancel(); // 关闭该定时器

	bool refresh(); // 刷新定时器触发时间 = 当前时间 + 定时周期

	bool reset(uint64_t ms, bool from_now); // 重置定时器，传入触发时间和标志位，是否从当前时间开始计算

private:
	// 私有构造，只能通过TimerManager定时器管理类进行创建
	Timer(uint64_t ms, std::function<void()> cb, bool recurring, TimerManager* manager);

	Timer(uint64_t next);

private:
	bool m_recurring = false; 				 // 是否为重复定时器
	uint64_t m_ms = 0; 						 // 循环周期
	uint64_t m_next = 0; 					 // 触发的精确时间，定时器创建的时间 + 定时周期
	std::function<void()> m_cb; 			 // 触发的回调函数
	TimerManager* m_manager = nullptr; 		 // 所属的管理器指针

private:
	// 用于存于STL中的比较函数
	struct Comparator
	{
		bool operator()(const Timer::ptr& lhs, const Timer::ptr& rhs) const;
	};

}; // class Timer end

// 定时器Timer的管理类
class TimerManager
{

friend class Timer;

public:
	typedef RWMutex RWMutexType;

	TimerManager();

	virtual ~TimerManager();

	// 传入触发时间、触发回调、是否为重复定时器来添加一个定时器
	Timer::ptr addTimer(uint64_t ms, std::function<void()> cb
						,bool recurring = false);

	// 添加一个条件定时器，weak_cond是判断条件
	Timer::ptr addConditionTimer(uint64_t ms, std::function<void()> cb
						,std::weak_ptr<void> weak_cond
						,bool recurring = false);

	// 获得下一次定时触发时间
	uint64_t getNextTimer();

	// 获取已触发定时器的回调函数，并将其插入任务队列
	void listExpiredCb(std::vector<std::function<void()> >& cbs);

	// 查询是否还有未完成的定时任务
	bool hasTimer();

protected:

	virtual void onTimerInsertedAtFront() = 0;

	// 添加定时器，addTimer的底层接口
	void addTimer(Timer::ptr val, RWMutexType::WriteLock& lock);

private:

	// 检测服务器时间是否被调后了
	bool detectClockRollover(uint64_t now_ms);

private:
	RWMutexType m_mutex; 									// 读写锁互斥量
	std::set<Timer::ptr, Timer::Comparator> m_timers; 		// 存储定时器的容器，默认按触发时间排序
	bool m_tickled = false; 								// 是否触发onTimerInsertedAtFront
	uint64_t m_previouseTime = 0; 							// 上一次的执行时间

}; // class TimerManager end

} // namespace shiosylar end

#endif
