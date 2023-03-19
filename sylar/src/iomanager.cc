#include "../include/iomanager.h"
#include "../include/macro.h"
#include "../include/logger.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <string.h>
#include <unistd.h>

namespace shiosylar
{

// 全局的系统日志器
static shiosylar::Logger::ptr g_logger = LOG_NAME("system");

enum EpollCtlOp {  };

static std::ostream& operator<< (std::ostream& os, const EpollCtlOp& op)
{
	switch((int)op)
	{
#define XX(ctl) \
		case ctl: \
			return os << #ctl;
		XX(EPOLL_CTL_ADD);
		XX(EPOLL_CTL_MOD);
		XX(EPOLL_CTL_DEL);
		default:
			return os << (int)op;
	}
#undef XX
}

static std::ostream& operator<< (std::ostream& os, EPOLL_EVENTS events)
{
	if(!events)
		return os << "0";

	bool first = true;
#define XX(E) \
	if(events & E) { \
		if(!first) { \
			os << "|"; \
		} \
		os << #E; \
		first = false; \
	}
	XX(EPOLLIN);
	XX(EPOLLPRI);
	XX(EPOLLOUT);
	XX(EPOLLRDNORM);
	XX(EPOLLRDBAND);
	XX(EPOLLWRNORM);
	XX(EPOLLWRBAND);
	XX(EPOLLMSG);
	XX(EPOLLERR);
	XX(EPOLLHUP);
	XX(EPOLLRDHUP);
	XX(EPOLLONESHOT);
	XX(EPOLLET);
#undef XX
	return os;
}

// 获取事件回调
IOManager::FdContext::EventContext& IOManager::FdContext::getContext(IOManager::Event event)
{
	switch(event)
	{
		case IOManager::READ:
			return read;
		case IOManager::WRITE:
			return write;
		default:
			ASSERT2(false, "getContext");
	}
	throw std::invalid_argument("getContext invalid event");
}

// 重置事件回调
void IOManager::FdContext::resetContext(EventContext& ctx)
{
	ctx.scheduler = nullptr;
	ctx.fiber.reset();
	ctx.cb = nullptr;
}

// 触发事件回调
void IOManager::FdContext::triggerEvent(IOManager::Event event)
{
	ASSERT(events & event);

	// 触发事件后，在fd上下文中去除该事件
	events = (Event)(events & ~event);

	EventContext& ctx = getContext(event); // 获取事件回调

	if(ctx.cb) // 插入任务队列
		ctx.scheduler->schedule(&ctx.cb);
	else
		ctx.scheduler->schedule(&ctx.fiber);

	ctx.scheduler = nullptr;
	return;
}

IOManager::IOManager(size_t threads, bool use_caller, const std::string& name)
	:Scheduler(threads, use_caller, name)
{
	m_epfd = epoll_create(5000); // 创建epollfd
	ASSERT(m_epfd > 0);

	int rt = pipe(m_tickleFds); // 创建管道，0端读取、1端写入
	ASSERT(!rt);

	epoll_event event;
	memset(&event, 0, sizeof(epoll_event));
	event.events = EPOLLIN | EPOLLET;
	event.data.fd = m_tickleFds[0];

	rt = fcntl(m_tickleFds[0], F_SETFL, O_NONBLOCK); // 设置管道读取端非阻塞
	ASSERT(!rt);

	rt = epoll_ctl(m_epfd, EPOLL_CTL_ADD, m_tickleFds[0], &event); // 将管道读取端加入epoll监听
	ASSERT(!rt);

	contextResize(32); // 初始化32个fd上下文对象在容器中

	start();
}

IOManager::~IOManager()
{
	stop();
	close(m_epfd); // 关闭epollfd
	close(m_tickleFds[0]); // 关闭管道的读写端
	close(m_tickleFds[1]);

	// 释放掉容器中的fd上下文
	for(size_t i = 0; i < m_fdContexts.size(); ++i)
	{
		if(m_fdContexts[i])
			delete m_fdContexts[i];
	}
}

// 设置fd上下文容器的容量
void IOManager::contextResize(size_t size)
{
	m_fdContexts.resize(size);

	for(size_t i = 0; i < m_fdContexts.size(); ++i)
	{
		if(!m_fdContexts[i]) // 如果没有创建，则在这里进行创建
		{
			m_fdContexts[i] = new FdContext;
			m_fdContexts[i]->fd = i;
		}
	}
}

// 向epoll添加事件
int IOManager::addEvent(int fd, Event event, std::function<void()> cb)
{
	FdContext* fd_ctx = nullptr;
	RWMutexType::ReadLock lock(m_mutex);

	// 查询是否已经初始化该fd标识的对象
	if((int)m_fdContexts.size() > fd)
	{
		fd_ctx = m_fdContexts[fd];
		lock.unlock();
	}
	else // 没有则进行1.5倍的初始化扩容
	{
		lock.unlock();
		RWMutexType::WriteLock lock2(m_mutex);
		contextResize(fd * 1.5);
		fd_ctx = m_fdContexts[fd];
	}

	FdContext::MutexType::Lock lock2(fd_ctx->mutex);

	// 如果该fd上下文已绑定了事件，记错错误日志，新的fd上不应该有事件
	if(UNLIKELY(fd_ctx->events & event))
	{
		LOG_ERROR(g_logger) << "addEvent assert fd=" << fd
					<< " event=" << (EPOLL_EVENTS)event
					<< " fd_ctx.event=" << (EPOLL_EVENTS)fd_ctx->events;
		ASSERT(!(fd_ctx->events & event));
	}

	int op = fd_ctx->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
	epoll_event epevent;
	epevent.events = EPOLLET | fd_ctx->events | event;
	epevent.data.ptr = fd_ctx;

	int rt = epoll_ctl(m_epfd, op, fd, &epevent); // 向epoll添加事件
	if(rt)
	{
		LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
			<< (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
			<< rt << " (" << errno << ") (" << strerror(errno) << ") fd_ctx->events="
			<< (EPOLL_EVENTS)fd_ctx->events;
		return -1;
	}

	++m_pendingEventCount; // 事件数量加一

	// 设置fd上下文的事件
	fd_ctx->events = (Event)(fd_ctx->events | event);

	// 获取事件对应的回调
	FdContext::EventContext& event_ctx = fd_ctx->getContext(event);
	ASSERT(!event_ctx.scheduler && !event_ctx.fiber && !event_ctx.cb);

	event_ctx.scheduler = Scheduler::GetThis(); // 设置调度器对象指针
	if(cb)
		event_ctx.cb.swap(cb); // 设置该事件的回调
	else
	{
		event_ctx.fiber = Fiber::GetThis();
		ASSERT2(event_ctx.fiber->getState() == Fiber::EXEC, "state=" << event_ctx.fiber->getState());
	}
	return 0;
}

// 从epoll中删除事件，清除事件的回调
bool IOManager::delEvent(int fd, Event event)
{
	RWMutexType::ReadLock lock(m_mutex);
	if((int)m_fdContexts.size() <= fd) // 容器中不存在该fd，直接返回false
		return false;

	FdContext* fd_ctx = m_fdContexts[fd];
	lock.unlock();

	FdContext::MutexType::Lock lock2(fd_ctx->mutex);
	if(UNLIKELY(!(fd_ctx->events & event)))
		return false;

	Event new_events = (Event)(fd_ctx->events & ~event); // 取消该事件
	int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL; // 如果取反后为空值，则从epoll中删除该fd
	epoll_event epevent;
	epevent.events = EPOLLET | new_events;
	epevent.data.ptr = fd_ctx;

	int rt = epoll_ctl(m_epfd, op, fd, &epevent);
	if(rt)
	{
		LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
			<< (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
			<< rt << " (" << errno << ") (" << strerror(errno) << ")";
		return false;
	}

	--m_pendingEventCount; // 活跃事件数量减一
	fd_ctx->events = new_events;
	FdContext::EventContext& event_ctx = fd_ctx->getContext(event); // 获取该事件回调
	fd_ctx->resetContext(event_ctx); // 重置该回调
	return true;
}

// 在epoll中关闭事件，触发该事件回调，且不清除回调
bool IOManager::cancelEvent(int fd, Event event)
{
	RWMutexType::ReadLock lock(m_mutex);
	if((int)m_fdContexts.size() <= fd)
		return false;

	FdContext* fd_ctx = m_fdContexts[fd];
	lock.unlock();

	FdContext::MutexType::Lock lock2(fd_ctx->mutex);
	if(UNLIKELY(!(fd_ctx->events & event)))
		return false;

	Event new_events = (Event)(fd_ctx->events & ~event);
	int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
	epoll_event epevent;
	epevent.events = EPOLLET | new_events;
	epevent.data.ptr = fd_ctx;

	int rt = epoll_ctl(m_epfd, op, fd, &epevent);
	if(rt)
	{
		LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
			<< (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
			<< rt << " (" << errno << ") (" << strerror(errno) << ")";
		return false;
	}

	fd_ctx->triggerEvent(event); // 触发事件回调
	--m_pendingEventCount; // epoll活跃事件数量减一
	return true;
}

// 关闭 fd 上的所有事件，会触发回调
bool IOManager::cancelAll(int fd)
{
	RWMutexType::ReadLock lock(m_mutex);
	if((int)m_fdContexts.size() <= fd) // fd 不在容器中，直接返回false
		return false;

	FdContext* fd_ctx = m_fdContexts[fd];
	lock.unlock();

	FdContext::MutexType::Lock lock2(fd_ctx->mutex);
	if(!fd_ctx->events)
		return false;

	int op = EPOLL_CTL_DEL;
	epoll_event epevent;
	epevent.events = 0;
	epevent.data.ptr = fd_ctx;

	int rt = epoll_ctl(m_epfd, op, fd, &epevent);
	if(rt)
	{
		LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
			<< (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
			<< rt << " (" << errno << ") (" << strerror(errno) << ")";
		return false;
	}

	if(fd_ctx->events & READ)
	{
		fd_ctx->triggerEvent(READ); // 触发事件回调
		--m_pendingEventCount;
	}
	if(fd_ctx->events & WRITE)
	{
		fd_ctx->triggerEvent(WRITE); // 触发事件回调
		--m_pendingEventCount;
	}

	ASSERT(fd_ctx->events == 0);
	return true;
}

// 返回当前IOManager的指针
IOManager* IOManager::GetThis()
{
	return dynamic_cast<IOManager*>(Scheduler::GetThis());
}

// 向管道中写数据，唤醒epoll_wait
void IOManager::tickle()
{
	if(!hasIdleThreads())
		return;

	int rt = write(m_tickleFds[1], "T", 1);
	ASSERT(rt == 1);
}

// 判断IOManager是否可以停止
bool IOManager::stopping(uint64_t& timeout)
{
	timeout = getNextTimer();

	// 当没有定时任务、epoll中没有监听事件、Scheduler停止运行时，表明IOManager停止工作了
	return timeout == ~0ull && m_pendingEventCount == 0 && Scheduler::stopping();
}

// 判断IOManager是否可以停止
bool IOManager::stopping()
{
	uint64_t timeout = 0;
	return stopping(timeout);
}

// 忙等待函数，会进入epoll_wait， 协程无任务可调度时执行idle协程
void IOManager::idle()
{
	LOG_DEBUG(g_logger) << "idle";
	const uint64_t MAX_EVNETS = 256;
	epoll_event* events = new epoll_event[MAX_EVNETS]();
	std::shared_ptr<epoll_event> shared_events(events, [](epoll_event* ptr){ delete[] ptr; });

	while(true)
	{
		uint64_t next_timeout = 0;
		if(UNLIKELY(stopping(next_timeout)))
		{
			LOG_INFO(g_logger) << "name=" << getName() << " idle stopping exit";
			break;
		}

		int rt = 0;
		do
		{
			static const int MAX_TIMEOUT = 3000;

			// 下一次定时器触发时间小于MAX_TIMEOUT，则使用next_timeout作为epoll_wait的超时时间
			if(next_timeout != ~0ull)
				next_timeout = (int)next_timeout > MAX_TIMEOUT ? MAX_TIMEOUT : next_timeout;
			else
				next_timeout = MAX_TIMEOUT;

			rt = epoll_wait(m_epfd, events, MAX_EVNETS, (int)next_timeout);
			if(rt < 0 && errno == EINTR) // 程序收到信号时，设置errno为EINTR，此时不做处理
			{
			}
			else // 其他情况结束循环
				break;

		} while(true);

		std::vector<std::function<void()> > cbs;
		listExpiredCb(cbs); // 取出触发的定时任务，存到容器 cbs 中
		if(!cbs.empty())
		{
			schedule(cbs.begin(), cbs.end()); // 将定时器任务插入到协程的任务队列中
			cbs.clear();
		}

		for(int i = 0; i < rt; ++i)
		{
			epoll_event& event = events[i];
			if(event.data.fd == m_tickleFds[0])
			{
				uint8_t dummy[256];
				while(read(m_tickleFds[0], dummy, sizeof(dummy)) > 0); // ET模式下，需要将数据读取完
				continue;
			}

			FdContext* fd_ctx = (FdContext*)event.data.ptr;
			FdContext::MutexType::Lock lock(fd_ctx->mutex);
			if(event.events & (EPOLLERR | EPOLLHUP))
				event.events |= (EPOLLIN | EPOLLOUT) & fd_ctx->events;

			int real_events = NONE; // 获取触发的事件
			if(event.events & EPOLLIN)
				real_events |= READ;

			if(event.events & EPOLLOUT)
				real_events |= WRITE;

			if((fd_ctx->events & real_events) == NONE)
				continue;

			int left_events = (fd_ctx->events & ~real_events); // 将刚才触发的事件清除，取消监听
			int op = left_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
			event.events = EPOLLET | left_events;

			int rt2 = epoll_ctl(m_epfd, op, fd_ctx->fd, &event);
			if(rt2)
			{
				LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
					<< (EpollCtlOp)op << ", " << fd_ctx->fd << ", " << (EPOLL_EVENTS)event.events << "):"
					<< rt2 << " (" << errno << ") (" << strerror(errno) << ")";
				continue;
			}

			if(real_events & READ)
			{
				fd_ctx->triggerEvent(READ);
				--m_pendingEventCount;
			}
			if(real_events & WRITE)
			{
				fd_ctx->triggerEvent(WRITE);
				--m_pendingEventCount;
			}
		}

		Fiber::ptr cur = Fiber::GetThis();
		auto raw_ptr = cur.get();
		cur.reset();

		raw_ptr->swapOut();
	}
}

// 当有新的定时器插入到定时器的首部,执行该函数
void IOManager::onTimerInsertedAtFront()
{
	tickle(); // 唤醒epoll_wait更新定时器
}

} // namespace shiosylar end
