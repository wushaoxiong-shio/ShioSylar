#include "../include/hook.h"
#include <dlfcn.h>

#include "../include/config.h"
#include "../include/logger.h"
#include "../include/fiber.h"
#include "../include/iomanager.h"
#include "../include/fd_manager.h"
#include "../include/macro.h"

shiosylar::Logger::ptr g_logger = LOG_NAME("system");

namespace shiosylar
{

static shiosylar::ConfigVar<int>::ptr g_tcp_connect_timeout =
    shiosylar::Config::Lookup("tcp.connect.timeout", 5000, "tcp connect timeout");

// 线程初始化为没有被hook
static thread_local bool t_hook_enable = false;

#define HOOK_FUN(XX) \
    XX(sleep) \
    XX(usleep) \
    XX(nanosleep) \
    XX(socket) \
    XX(connect) \
    XX(accept) \
    XX(read) \
    XX(readv) \
    XX(recv) \
    XX(recvfrom) \
    XX(recvmsg) \
    XX(write) \
    XX(writev) \
    XX(send) \
    XX(sendto) \
    XX(sendmsg) \
    XX(close) \
    XX(fcntl) \
    XX(ioctl) \
    XX(getsockopt) \
    XX(setsockopt)

// hook上述定义的库函数
// dlsym 传入函数名称，返回该函数在链接的动态库中的地址
// 将其赋值给相应的 name_f 函数指针
// 然后自定义上述的库函数，在自定义函数中再调用name_f函数，实现hook
void hook_init()
{
    static bool is_inited = false;
    if(is_inited)
        return;

// 和上述的函数列表进行配合使用，获取库函数的运行地址，使用name_f进行调用
#define XX(name) name ## _f = (name ## _fun)dlsym(RTLD_NEXT, #name);
    HOOK_FUN(XX);
#undef XX

}

static uint64_t s_connect_timeout = -1;
struct _HookIniter
{
    _HookIniter()
    {
        hook_init();
        s_connect_timeout = g_tcp_connect_timeout->getValue();

        g_tcp_connect_timeout->addListener([](const int& old_value, const int& new_value){
                LOG_INFO(g_logger) << "tcp connect timeout changed from "
                                         << old_value << " to " << new_value;
                s_connect_timeout = new_value;
        });
    }
};

static _HookIniter s_hook_initer;

bool is_hook_enable()
{
    return t_hook_enable;
}

void set_hook_enable(bool flag)
{
    t_hook_enable = flag;
}

}

// 记录是否超时的标志位
struct timer_info
{
    int cancelled = 0;
};

template<typename OriginFun, typename... Args>
static ssize_t do_io(int fd, OriginFun fun, const char* hook_fun_name,
        uint32_t event, int timeout_so, Args&&... args)
{
    // 如果当前线程没有被hook，则直接调用
    if(!shiosylar::t_hook_enable)
        return fun(fd, std::forward<Args>(args)...);

    // 判断该fd是否在fd容器中，不在则直接调用
    shiosylar::FdCtx::ptr ctx = shiosylar::FdMgr::GetInstance()->get(fd);
    if(!ctx)
        return fun(fd, std::forward<Args>(args)...);

    // 如果当前的fd已关闭，则直接设置错误并返回
    if(ctx->isClose())
    {
        errno = EBADF;
        return -1;
    }

    // 不是socketfd或者用户设置了非阻塞，直接调用
    if(!ctx->isSocket() || ctx->getUserNonblock())
        return fun(fd, std::forward<Args>(args)...);

    // 获取超时时间，根据timeout_so的值返回读或写的超时时间
    uint64_t to = ctx->getTimeout(timeout_so);
    std::shared_ptr<timer_info> tinfo(new timer_info);

/*
运行逻辑：
先调用一次IO函数，根据是否返回值来判断
当n == -1 && errno == EAGAIN时说明IO未就绪
此时需要加入到IOManager监听列表中进行等待触发

此时先检查该函数是否设置超时时间
如果有超时时间，就额外为该fd添加一个定时器事件
超时了就设置标志位并关闭IOManage的IO事件

之后向IOManage添加事件，并让出CPU
当超时或IO事件触发时，会切换回来
判断一下，如果是超时则设置错误码并直接返回
如果是IO触发则goto到此进行IO函数的调用
*/

retry:
    // 执行一下函数，看是否会返回结果
    ssize_t n = fun(fd, std::forward<Args>(args)...);

    // EINTR表示函数被信号中断了，继续执行
    while(n == -1 && errno == EINTR)
        n = fun(fd, std::forward<Args>(args)...);

    // EAGAIN表示IO未就绪，此时需要让出CPU，该任务需要被挂起
    if(n == -1 && errno == EAGAIN)
    {
        shiosylar::IOManager* iom = shiosylar::IOManager::GetThis();

        // 创建一个定时任务
        shiosylar::Timer::ptr timer;
        std::weak_ptr<timer_info> winfo(tinfo);

        // 该fd设置了超时时间
        if(to != (uint64_t)-1)
        {
            // 向IOManage添加一个定时任务
            timer = iom->addConditionTimer(to, [winfo, fd, iom, event]() {
                auto t = winfo.lock();
                if(!t || t->cancelled) // 如果标志位不存在或者已经超时，则直接返回
                    return;

                t->cancelled = ETIMEDOUT; // 设置已经超时

                // 关闭该fd上的事件
                iom->cancelEvent(fd, (shiosylar::IOManager::Event)(event));
            }, winfo);
        }

        // 向IOManage添加上fd的事件
        int rt = iom->addEvent(fd, (shiosylar::IOManager::Event)(event));

        if(UNLIKELY(rt)) // 添加事件失败
        {
            LOG_ERROR(g_logger) << hook_fun_name << " addEvent("
                << fd << ", " << event << ")";
            if(timer)
                timer->cancel();

            return -1;
        }
        else
        {
            shiosylar::Fiber::YieldToHold(); // 让出CPU

            // 切回来，可能是超时也可能是IO触发
            if(timer) // 如果有定时任务，先取消任务
                timer->cancel();

            if(tinfo->cancelled) // 判断是否超时
            {
                errno = tinfo->cancelled; // 设置超时错误码
                return -1;
            }
            goto retry; // 回到上述，此时事件已经触发，可以执行IO函数了
        }
    }
    return n;
}


extern "C"
{

// 把要hook的函数指针创建出来，并初始化置空
#define XX(name) name ## _fun name ## _f = nullptr;
    HOOK_FUN(XX);
#undef XX

unsigned int sleep(unsigned int seconds)
{
    if(!shiosylar::t_hook_enable)
        return sleep_f(seconds);

    shiosylar::Fiber::ptr fiber = shiosylar::Fiber::GetThis();
    shiosylar::IOManager* iom = shiosylar::IOManager::GetThis();
    iom->addTimer(seconds * 1000, std::bind((void(shiosylar::Scheduler::*)
            (shiosylar::Fiber::ptr, int thread))&shiosylar::IOManager::schedule
            ,iom, fiber, -1));
    shiosylar::Fiber::YieldToHold();
    return 0;
}

int usleep(useconds_t usec)
{
    if(!shiosylar::t_hook_enable)
        return usleep_f(usec);

    shiosylar::Fiber::ptr fiber = shiosylar::Fiber::GetThis();
    shiosylar::IOManager* iom = shiosylar::IOManager::GetThis();
    iom->addTimer(usec / 1000, std::bind((void(shiosylar::Scheduler::*)
            (shiosylar::Fiber::ptr, int thread))&shiosylar::IOManager::schedule
            ,iom, fiber, -1));
    shiosylar::Fiber::YieldToHold();
    return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem)
{
    if(!shiosylar::t_hook_enable)
        return nanosleep_f(req, rem);

    int timeout_ms = req->tv_sec * 1000 + req->tv_nsec / 1000 /1000;
    shiosylar::Fiber::ptr fiber = shiosylar::Fiber::GetThis();
    shiosylar::IOManager* iom = shiosylar::IOManager::GetThis();
    iom->addTimer(timeout_ms, std::bind((void(shiosylar::Scheduler::*)
            (shiosylar::Fiber::ptr, int thread))&shiosylar::IOManager::schedule
            ,iom, fiber, -1));
    shiosylar::Fiber::YieldToHold();
    return 0;
}

int socket(int domain, int type, int protocol)
{
    if(!shiosylar::t_hook_enable)
        return socket_f(domain, type, protocol);

    int fd = socket_f(domain, type, protocol);
    if(fd == -1)
        return fd;

    shiosylar::FdMgr::GetInstance()->get(fd, true);
    return fd;
}

int connect_with_timeout(int fd, const struct sockaddr* addr, socklen_t addrlen, uint64_t timeout_ms) {
    if(!shiosylar::t_hook_enable)
        return connect_f(fd, addr, addrlen);

    shiosylar::FdCtx::ptr ctx = shiosylar::FdMgr::GetInstance()->get(fd);
    if(!ctx || ctx->isClose())
    {
        errno = EBADF;
        return -1;
    }

    if(!ctx->isSocket())
        return connect_f(fd, addr, addrlen);


    if(ctx->getUserNonblock())
        return connect_f(fd, addr, addrlen);


    int n = connect_f(fd, addr, addrlen);
    if(n == 0)
    {
        return 0;
    }
    else if(n != -1 || errno != EINPROGRESS)
    {
        return n;
    }

    shiosylar::IOManager* iom = shiosylar::IOManager::GetThis();
    shiosylar::Timer::ptr timer;
    std::shared_ptr<timer_info> tinfo(new timer_info);
    std::weak_ptr<timer_info> winfo(tinfo);

    if(timeout_ms != (uint64_t)-1)
    {
        timer = iom->addConditionTimer(timeout_ms, [winfo, fd, iom]() {
                auto t = winfo.lock();
                if(!t || t->cancelled)
                    return;

                t->cancelled = ETIMEDOUT;
                iom->cancelEvent(fd, shiosylar::IOManager::WRITE);
        }, winfo);
    }

    int rt = iom->addEvent(fd, shiosylar::IOManager::WRITE);
    if(rt == 0) {
        shiosylar::Fiber::YieldToHold();
        if(timer)
            timer->cancel();

        if(tinfo->cancelled)
        {
            errno = tinfo->cancelled;
            return -1;
        }
    }
    else
    {
        if(timer)
            timer->cancel();

        LOG_ERROR(g_logger) << "connect addEvent(" << fd << ", WRITE) error";
    }

    int error = 0;
    socklen_t len = sizeof(int);
    if(-1 == getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len))
        return -1;

    if(!error)
    {
        return 0;
    }
    else
    {
        errno = error;
        return -1;
    }
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    return connect_with_timeout(sockfd, addr, addrlen, shiosylar::s_connect_timeout);
}

int accept(int s, struct sockaddr *addr, socklen_t *addrlen)
{
    int fd = do_io(s, accept_f, "accept", shiosylar::IOManager::READ, SO_RCVTIMEO, addr, addrlen);
    if(fd >= 0)
        shiosylar::FdMgr::GetInstance()->get(fd, true);

    return fd;
}

ssize_t read(int fd, void *buf, size_t count)
{
    return do_io(fd, read_f, "read", shiosylar::IOManager::READ, SO_RCVTIMEO, buf, count);
}

ssize_t readv(int fd, const struct iovec *iov, int iovcnt)
{
    return do_io(fd, readv_f, "readv", shiosylar::IOManager::READ, SO_RCVTIMEO, iov, iovcnt);
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags)
{
    return do_io(sockfd, recv_f, "recv", shiosylar::IOManager::READ, SO_RCVTIMEO, buf, len, flags);
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen)
{
    return do_io(sockfd, recvfrom_f, "recvfrom", shiosylar::IOManager::READ, SO_RCVTIMEO, buf, len, flags, src_addr, addrlen);
}

ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags)
{
    return do_io(sockfd, recvmsg_f, "recvmsg", shiosylar::IOManager::READ, SO_RCVTIMEO, msg, flags);
}

ssize_t write(int fd, const void *buf, size_t count)
{
    return do_io(fd, write_f, "write", shiosylar::IOManager::WRITE, SO_SNDTIMEO, buf, count);
}

ssize_t writev(int fd, const struct iovec *iov, int iovcnt)
{
    return do_io(fd, writev_f, "writev", shiosylar::IOManager::WRITE, SO_SNDTIMEO, iov, iovcnt);
}

ssize_t send(int s, const void *msg, size_t len, int flags)
{
    return do_io(s, send_f, "send", shiosylar::IOManager::WRITE, SO_SNDTIMEO, msg, len, flags);
}

ssize_t sendto(int s, const void *msg, size_t len, int flags, const struct sockaddr *to, socklen_t tolen)
{
    return do_io(s, sendto_f, "sendto", shiosylar::IOManager::WRITE, SO_SNDTIMEO, msg, len, flags, to, tolen);
}

ssize_t sendmsg(int s, const struct msghdr *msg, int flags)
{
    return do_io(s, sendmsg_f, "sendmsg", shiosylar::IOManager::WRITE, SO_SNDTIMEO, msg, flags);
}

int close(int fd)
{
    if(!shiosylar::t_hook_enable)
        return close_f(fd);

    shiosylar::FdCtx::ptr ctx = shiosylar::FdMgr::GetInstance()->get(fd);
    if(ctx)
    {
        auto iom = shiosylar::IOManager::GetThis();
        if(iom)
            iom->cancelAll(fd);

        shiosylar::FdMgr::GetInstance()->del(fd);
    }
    return close_f(fd);
}

int fcntl(int fd, int cmd, ... /* arg */ )
{
    va_list va;
    va_start(va, cmd);
    switch(cmd)
    {
        case F_SETFL:
            {
                int arg = va_arg(va, int);
                va_end(va);
                shiosylar::FdCtx::ptr ctx = shiosylar::FdMgr::GetInstance()->get(fd);
                if(!ctx || ctx->isClose() || !ctx->isSocket())
                    return fcntl_f(fd, cmd, arg);

                ctx->setUserNonblock(arg & O_NONBLOCK);
                if(ctx->getSysNonblock())
                {
                    arg |= O_NONBLOCK;
                }
                else
                {
                    arg &= ~O_NONBLOCK;
                }
                return fcntl_f(fd, cmd, arg);
            }
            break;
        case F_GETFL:
            {
                va_end(va);
                int arg = fcntl_f(fd, cmd);
                shiosylar::FdCtx::ptr ctx = shiosylar::FdMgr::GetInstance()->get(fd);
                if(!ctx || ctx->isClose() || !ctx->isSocket())
                    return arg;

                if(ctx->getUserNonblock())
                {
                    return arg | O_NONBLOCK;
                }
                else
                {
                    return arg & ~O_NONBLOCK;
                }
            }
            break;
        case F_DUPFD:
        case F_DUPFD_CLOEXEC:
        case F_SETFD:
        case F_SETOWN:
        case F_SETSIG:
        case F_SETLEASE:
        case F_NOTIFY:
#ifdef F_SETPIPE_SZ
        case F_SETPIPE_SZ:
#endif
            {
                int arg = va_arg(va, int);
                va_end(va);
                return fcntl_f(fd, cmd, arg); 
            }
            break;
        case F_GETFD:
        case F_GETOWN:
        case F_GETSIG:
        case F_GETLEASE:
#ifdef F_GETPIPE_SZ
        case F_GETPIPE_SZ:
#endif
            {
                va_end(va);
                return fcntl_f(fd, cmd);
            }
            break;
        case F_SETLK:
        case F_SETLKW:
        case F_GETLK:
            {
                struct flock* arg = va_arg(va, struct flock*);
                va_end(va);
                return fcntl_f(fd, cmd, arg);
            }
            break;
        case F_GETOWN_EX:
        case F_SETOWN_EX:
            {
                struct f_owner_exlock* arg = va_arg(va, struct f_owner_exlock*);
                va_end(va);
                return fcntl_f(fd, cmd, arg);
            }
            break;
        default:
            va_end(va);
            return fcntl_f(fd, cmd);
    }
}

int ioctl(int d, unsigned long int request, ...)
{
    va_list va;
    va_start(va, request);
    void* arg = va_arg(va, void*);
    va_end(va);

    if(FIONBIO == request)
    {
        bool user_nonblock = !!*(int*)arg;
        shiosylar::FdCtx::ptr ctx = shiosylar::FdMgr::GetInstance()->get(d);
        if(!ctx || ctx->isClose() || !ctx->isSocket())
            return ioctl_f(d, request, arg);

        ctx->setUserNonblock(user_nonblock);
    }
    return ioctl_f(d, request, arg);
}

int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen)
{
    return getsockopt_f(sockfd, level, optname, optval, optlen);
}

int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen)
{
    if(!shiosylar::t_hook_enable)
        return setsockopt_f(sockfd, level, optname, optval, optlen);

    if(level == SOL_SOCKET)
    {
        if(optname == SO_RCVTIMEO || optname == SO_SNDTIMEO)
        {
            shiosylar::FdCtx::ptr ctx = shiosylar::FdMgr::GetInstance()->get(sockfd);
            if(ctx)
            {
                const timeval* v = (const timeval*)optval;
                ctx->setTimeout(optname, v->tv_sec * 1000 + v->tv_usec / 1000);
            }
        }
    }
    return setsockopt_f(sockfd, level, optname, optval, optlen);
}

}
