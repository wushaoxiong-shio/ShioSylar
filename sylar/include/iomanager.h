#ifndef __SHIOSYLAR_IOMANAGER_H__
#define __SHIOSYLAR_IOMANAGER_H__

// IO管理类，继承于 Scheduler 、TimerManager

#include "scheduler.h"
#include "timer.h"

namespace shiosylar
{

class IOManager : public Scheduler, public TimerManager
{

public:
	typedef std::shared_ptr<IOManager> ptr;
	typedef RWMutex RWMutexType;

	enum Event // epoll 事件类型
	{
		NONE    = 0x0, 		// 空事件
		READ    = 0x1, 		// 读事件
		WRITE   = 0x4, 		// 写事件
	};

private:
	struct FdContext // fd上下文
	{
		typedef Mutex MutexType;

		struct EventContext // 事件回调
		{
			Scheduler* scheduler = nullptr;
			Fiber::ptr fiber;
			std::function<void()> cb;
		};

		// 传入事件类型，获取事件回调
		EventContext& getContext(Event event);

		// 重置事件回调
		void resetContext(EventContext& ctx);

		// 触发事件回调
		void triggerEvent(Event event);

		EventContext read; 			// 读事件触发回调
		EventContext write; 		// 写事件触发回调
		int fd = 0; 				// fd描述符
		Event events = NONE; 		// 监听的事件
		MutexType mutex; 			// 互斥量

	}; // struct FdContext end

public:
	IOManager(size_t threads = 1, bool use_caller = true, const std::string& name = "");

	~IOManager();

	// 添加 fd 上的事件
	int addEvent(int fd, Event event, std::function<void()> cb = nullptr);

	// 删除 fd 上的事件，会清除事件的回调
	bool delEvent(int fd, Event event);

	// 取消 fd 上的事件，会触发回调，且不清除回调
	bool cancelEvent(int fd, Event event);

	// 关闭 fd 上的所有事件
	bool cancelAll(int fd);

	// 获取 IOManager 对象指针
	static IOManager* GetThis();

protected:
	// 向管道中写数据，唤醒epoll_wait
	void tickle() override;

	// 判断IOManager是否可以停止
	bool stopping() override;

	// 忙等待函数，会进入epoll_wait， 协程无任务可调度时执行idle协程
	void idle() override;

	// 当有新的定时器插入到定时器的首部,执行该函数
	void onTimerInsertedAtFront() override;

	// 设置fd上下文容器的容量
	void contextResize(size_t size);

	// 判断IOManager是否可以停止
	bool stopping(uint64_t& timeout);

private:
	int m_epfd = 0; 									// epoll描述符
	int m_tickleFds[2]; 								// 命名管道，用于唤醒当前线程的epoll_wait
	std::atomic<size_t> m_pendingEventCount = {0}; 		// 活跃的事件数
	RWMutexType m_mutex; 								// 互斥量读写锁
	std::vector<FdContext*> m_fdContexts; 				// fd 容器

}; // class IOManager  end

} // namespace shiosylar end

#endif
