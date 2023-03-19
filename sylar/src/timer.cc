#include "../include/timer.h"
#include "../include/util.h"

namespace shiosylar
{

// 用于存于STL中的比较函数
bool Timer::Comparator::operator()(const Timer::ptr& lhs, const Timer::ptr& rhs) const
{
	if(!lhs && !rhs) // 两个指针为空
		return false;

	if(!lhs) // 左边空指针
		return true;

	if(!rhs) // 右边空指针
		return false;

	if(lhs->m_next < rhs->m_next) // 比较触发时间大小
		return true;

	if(rhs->m_next < lhs->m_next) // 比较触发时间大小
		return false;

	return lhs.get() < rhs.get(); // 如果触发时间相等，则比较在裸指针对象内存中的地址大小
}

// 定时器初始化
Timer::Timer(uint64_t ms, std::function<void()> cb, bool recurring, TimerManager* manager)
	:m_recurring(recurring)
	,m_ms(ms)
	,m_cb(cb)
	,m_manager(manager)
{
	// 初始化的时候计算触发的精确时间
	m_next = shiosylar::GetCurrentMS() + m_ms;
}

Timer::Timer(uint64_t next)
	:m_next(next)
{  }

// 关闭该定时器
bool Timer::cancel()
{
	TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
	if(m_cb)
	{
		m_cb = nullptr; // 回调函数置空
		auto it = m_manager->m_timers.find(shared_from_this());
		m_manager->m_timers.erase(it); // 从定时器容器中删除
		return true;
	}
	return false;
}

// 重新刷新触发时间
bool Timer::refresh()
{
	TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
	if(!m_cb) // 如果回调函数为空，则说明定时任务已经被执行了
		return false;

	auto it = m_manager->m_timers.find(shared_from_this());
	if(it == m_manager->m_timers.end())
		return false;

	m_manager->m_timers.erase(it); // 先删除，再插入，因为set容器要保持有序性
	m_next = shiosylar::GetCurrentMS() + m_ms; // 修改触发时间，加一个周期
	m_manager->m_timers.insert(shared_from_this()); // 重新插入容器，进行排序
	return true;
}

// 重置定时器
bool Timer::reset(uint64_t ms, bool from_now)
{
	if(ms == m_ms && !from_now) // 如果触发间隔不变，且不从当前时间开始，则直接返回true
		return true;

	TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
	if(!m_cb) // 回调函数指针为空，则说明该任务已经被执行了
		return false;

	auto it = m_manager->m_timers.find(shared_from_this());
	if(it == m_manager->m_timers.end())
		return false;

	m_manager->m_timers.erase(it);
	uint64_t start = 0;
	if(from_now)
		start = shiosylar::GetCurrentMS(); // 将当前时间设置为定时器创建的时间
	else
		start = m_next - m_ms; // 获取原先定时器创建的时间

	m_ms = ms; // 更新定时周期
	m_next = start + m_ms; // 重新设置定时器触发的精确时间
	m_manager->addTimer(shared_from_this(), lock); // 重新添加进定时器容器
	return true;
}

TimerManager::TimerManager()
{
	m_previouseTime = shiosylar::GetCurrentMS();
}

TimerManager::~TimerManager()
{  }

// 添加定时器
Timer::ptr TimerManager::addTimer(uint64_t ms, std::function<void()> cb, bool recurring)
{
	Timer::ptr timer(new Timer(ms, cb, recurring, this));
	RWMutexType::WriteLock lock(m_mutex);
	addTimer(timer, lock); // 调用底层接口
	return timer;
}

// 条件定时器的回调函数，执行任务前先判断条件是否成立
static void OnTimer(std::weak_ptr<void> weak_cond, std::function<void()> cb)
{
	std::shared_ptr<void> tmp = weak_cond.lock(); // 对weak进行提升，tmp为空说明条件不成立
	if(tmp)
		cb(); // 如果任务还在，则执行任务
}

// 添加一个条件定时器
Timer::ptr TimerManager::addConditionTimer(uint64_t ms, std::function<void()> cb
									,std::weak_ptr<void> weak_cond
									,bool recurring)
{
	return addTimer(ms, std::bind(&OnTimer, weak_cond, cb), recurring);
}

// 获取下一次触发时间
uint64_t TimerManager::getNextTimer()
{
	RWMutexType::ReadLock lock(m_mutex);
	m_tickled = false;
	if(m_timers.empty()) // 如果没有定时任务则返回一个极大的值
		return ~0ull;

	const Timer::ptr& next = *m_timers.begin();
	uint64_t now_ms = shiosylar::GetCurrentMS();
	if(now_ms >= next->m_next) // 当前时间大于第一个定时器的触发时间，返回0，马上执行
		return 0;
	else
		return next->m_next - now_ms; // 返回下一次触发时间
}

// 获取已触发定时器的回调函数，并将其插入任务队列
void TimerManager::listExpiredCb(std::vector<std::function<void()> >& cbs)
{
	uint64_t now_ms = shiosylar::GetCurrentMS();
	std::vector<Timer::ptr> expired;
	{
		RWMutexType::ReadLock lock(m_mutex);
		if(m_timers.empty()) // 没有定时任务，则直接返回
			return;
	}
	RWMutexType::WriteLock lock(m_mutex);
	if(m_timers.empty())
		return;

	bool rollover = detectClockRollover(now_ms); // 检查一下系统时间是否被调后

	// 如果系统时间没有被调后，且容器中第一个定时器没触发，则直接返回
	if(!rollover && ((*m_timers.begin())->m_next > now_ms))
		return;

	Timer::ptr now_timer(new Timer(now_ms));

	//如果系统时间被调后了，则直接执行所有的定时任务，否则只取出已触发的任务
	auto it = rollover ? m_timers.end() : m_timers.lower_bound(now_timer);

	// lower_bound 返回第一个大于等于的迭代器，如果后续存在相等的，要手动判断
	// 通过 while 将触发时间和现在相等的定时器也取出
	while(it != m_timers.end() && (*it)->m_next == now_ms)
		++it;

	expired.insert(expired.begin(), m_timers.begin(), it); // 取出所有的超时定时器
	m_timers.erase(m_timers.begin(), it); // 删除容器中已经超时的定时器
	cbs.reserve(expired.size());

	for(auto& timer : expired)
	{
		cbs.push_back(timer->m_cb); // 插入到预备的任务队列容器中
		if(timer->m_recurring) // 如果是重复的定时任务，则刷新触发时间，重新插入定时器容器中
		{
			timer->m_next = now_ms + timer->m_ms;
			m_timers.insert(timer);
		}
		else
			timer->m_cb = nullptr; // 一次性任务则将回调函数置空

	}
}


// 添加定时器，addTimer的底层接口
void TimerManager::addTimer(Timer::ptr val, RWMutexType::WriteLock& lock)
{
	auto it = m_timers.insert(val).first;

	// 添加定时器的时候检查该定时器是否容器中的第一个，第一个代表最先触发超时，需要更新下一次超时时间
	bool at_front = (it == m_timers.begin()) && !m_tickled;
	if(at_front)
		m_tickled = true;

	lock.unlock();

	if(at_front)
		onTimerInsertedAtFront();
}

// 检测服务器时间是否被调后了
bool TimerManager::detectClockRollover(uint64_t now_ms)
{
	bool rollover = false;

	// 如果上一次执行时间大于当前时间，并且上一次执行时间比现在超过了一个小时，认定系统时间改变了
	if(now_ms < m_previouseTime && now_ms < (m_previouseTime - 60 * 60 * 1000))
		rollover = true;

	m_previouseTime = now_ms; // 更新上一次执行时间为当前时间
	return rollover;
}

// 查询是否还有未完成的定时任务
bool TimerManager::hasTimer()
{
	RWMutexType::ReadLock lock(m_mutex);
	return !m_timers.empty();
}

} // namespace shiosylar end
