#include "../include/fiber.h"
#include "../include/config.h"
#include "../include/macro.h"
#include "../include/logger.h"
#include "../include/scheduler.h"

#include <atomic>

namespace shiosylar
{

// 全局日志类，名称为'system'
static Logger::ptr g_logger = LOG_NAME("system");

// 原子计数类--协程ID
static std::atomic<uint64_t> s_fiber_id {0};
// 原子计数类--已经创建的协程数量
static std::atomic<uint64_t> s_fiber_count {0};

// 线程私有变量--当前正在运行的协程指针
static thread_local Fiber* t_fiber = nullptr;
// 线程私有变量--主协程智能指针
static thread_local Fiber::ptr t_threadFiber = nullptr;
// 全局的配置器对象，协程的默认栈大小128 * 1024
static ConfigVar<uint32_t>::ptr g_fiber_stack_size =
    Config::Lookup<uint32_t>("fiber.stack_size", 128 * 1024, "fiber stack size");

// 内存接口封装类
class MallocStackAllocator
{
public:
    static void* Alloc(size_t size)
    {
        return malloc(size);
    }

    static void Dealloc(void* vp, size_t size)
    {
        return free(vp);
    }

};

using StackAllocator = MallocStackAllocator;

// 获取当前正在运行的协程ID
uint64_t Fiber::GetFiberId()
{
    if(t_fiber) // 如果当前没有协程对象则返回0
        return t_fiber->getId();
    return 0;
}

// 私有无参构造，静态调用创建单例主协程，主协程没有运行函数，主协程保存的是当前线程的上下文
Fiber::Fiber()
{
    m_state = EXEC;
    SetThis(this);
    if(getcontext(&m_ctx))
        ASSERT2(false, "getcontext");
    ++s_fiber_count;
    LOG_DEBUG(g_logger) << "Fiber::Fiber main";
}

// 共有有参构造，创建普通协程，并运行函数
Fiber::Fiber(std::function<void()> cb, size_t stacksize, bool use_caller)
                :
                m_id(++s_fiber_id),
                m_cb(cb)
{
    ++s_fiber_count;
    m_stacksize = stacksize ? stacksize : g_fiber_stack_size->getValue();

    m_stack = StackAllocator::Alloc(m_stacksize);
    if(getcontext(&m_ctx))
        ASSERT2(false, "getcontext");
    m_ctx.uc_link = nullptr;
    m_ctx.uc_stack.ss_sp = m_stack;
    m_ctx.uc_stack.ss_size = m_stacksize;

    if(!use_caller) // 是否使用use_caller，默认不用
        makecontext(&m_ctx, &Fiber::MainFunc, 0);
    else
        makecontext(&m_ctx, &Fiber::CallerMainFunc, 0);

    LOG_DEBUG(g_logger) << "Fiber::Fiber id=" << m_id;
}


Fiber::~Fiber()
{
    --s_fiber_count;
    if(m_stack)
    {
        ASSERT(m_state == TERM
                        || m_state == EXCEPT
                        || m_state == INIT);

        StackAllocator::Dealloc(m_stack, m_stacksize);
    }
    else
    {
        ASSERT(!m_cb); // 协程的运行函数执行后，m_cb必为空
        ASSERT(m_state == EXEC);

        Fiber* cur = t_fiber;
        if(cur == this)
            SetThis(nullptr);
    }
    LOG_DEBUG(g_logger) << "Fiber::~Fiber id=" << m_id
                              << " total=" << s_fiber_count;
}

//重置协程，并重置状态
void Fiber::reset(std::function<void()> cb)
{
    ASSERT(m_stack);
    // 只有在初始态、结束态、异常态的时候才能重置
    ASSERT(m_state == TERM
                    || m_state == EXCEPT
                    || m_state == INIT);
    m_cb = cb;
    if(getcontext(&m_ctx))
        ASSERT2(false, "getcontext");

    m_ctx.uc_link = nullptr;
    m_ctx.uc_stack.ss_sp = m_stack;
    m_ctx.uc_stack.ss_size = m_stacksize;

    makecontext(&m_ctx, &Fiber::MainFunc, 0);
    m_state = INIT; // 设置为初始化状态
}

// 切到当前协程
void Fiber::call()
{
    SetThis(this);
    m_state = EXEC;
    if(swapcontext(&t_threadFiber->m_ctx, &m_ctx))
        ASSERT2(false, "swapcontext");
}

// 切回到主协程
void Fiber::back()
{
    SetThis(t_threadFiber.get());
    if(swapcontext(&m_ctx, &t_threadFiber->m_ctx))
        ASSERT2(false, "swapcontext");
}

//切换到当前协程执行，从调度器协程切出
void Fiber::swapIn()
{
    SetThis(this); // 设置当前协程为正在运行的协程
    ASSERT(m_state != EXEC);
    m_state = EXEC;
    if(swapcontext(&Scheduler::GetMainFiber()->m_ctx, &m_ctx))
        ASSERT2(false, "swapcontext");
}

//切换到后台执行，切回到调度器协程
void Fiber::swapOut()
{
    SetThis(Scheduler::GetMainFiber()); // 设置主协程为正在运行的协程
    if(swapcontext(&m_ctx, &Scheduler::GetMainFiber()->m_ctx))
        ASSERT2(false, "swapcontext");
}

//设置当前协程为正在运行的协程
void Fiber::SetThis(Fiber* f)
{
    t_fiber = f;
}

// 获取当前正在运行的协程
Fiber::ptr Fiber::GetThis()
{
    if(t_fiber)
        return t_fiber->shared_from_this();
    Fiber::ptr main_fiber(new Fiber); // 如果当前没有协程，则创建一个主协程并返回
    ASSERT(t_fiber == main_fiber.get());
    t_threadFiber = main_fiber;
    return t_fiber->shared_from_this();
}

//协程切换到调度协程，并且设置为Ready状态
void Fiber::YieldToReady()
{
    Fiber::ptr cur = GetThis();
    ASSERT(cur->m_state == EXEC);
    cur->m_state = READY;
    cur->swapOut();
}

//协程切换到调度协程，并且设置为Hold状态
void Fiber::YieldToHold()
{
    Fiber::ptr cur = GetThis();
    ASSERT(cur->m_state == EXEC);
    //cur->m_state = HOLD;
    cur->swapOut();
}

// 获取总协程数
uint64_t Fiber::TotalFibers()
{
    return s_fiber_count;
}

// 协程运行的主函数，运行完切回调度协程
void Fiber::MainFunc()
{
    Fiber::ptr cur = GetThis(); // 这里获得了一个协程的智能指针，计数加一
    ASSERT(cur);
    try
    {
        cur->m_cb(); // function对象使用完置空
        cur->m_cb = nullptr;
        cur->m_state = TERM;
    }
    catch (std::exception& ex)
    {
        cur->m_state = EXCEPT; // 设置为异常状态
        LOG_ERROR(g_logger) << "Fiber Except: " << ex.what()
            << " fiber_id=" << cur->getId()
            << std::endl
            << shiosylar::BacktraceToString();
    }
    catch (...)
    {
        cur->m_state = EXCEPT; // 设置为异常状态
        LOG_ERROR(g_logger) << "Fiber Except"
            << " fiber_id=" << cur->getId()
            << std::endl
            << shiosylar::BacktraceToString();
    }

    auto raw_ptr = cur.get(); // 得到协程对象的裸指针
    cur.reset(); // 这里清除掉，计数减一，防止锁住对象导致不能自动析构

    // 通过裸指针调用切回主协程，切回调度协程后，这里的上下文暂停了，且不会再切回来
    // 前面不清除计数的话，这里的计数会一直存在，不会跳出作用域，就会锁住对象无法自动析构释放
    raw_ptr->swapOut();

    // 任务执行完毕了，不会再切回来，切回来就是出错了
    ASSERT2(false, "never reach fiber_id=" + std::to_string(raw_ptr->getId()));
}

// 使用use_caller时，协程的运行函数，运行完切回主协程
void Fiber::CallerMainFunc()
{
    Fiber::ptr cur = GetThis();
    ASSERT(cur);
    try
    {
        cur->m_cb();
        cur->m_cb = nullptr;
        cur->m_state = TERM;
    }
    catch (std::exception& ex)
    {
        cur->m_state = EXCEPT;
        LOG_ERROR(g_logger) << "Fiber Except: " << ex.what()
            << " fiber_id=" << cur->getId()
            << std::endl
            << shiosylar::BacktraceToString();
    } catch (...)
    {
        cur->m_state = EXCEPT;
        LOG_ERROR(g_logger) << "Fiber Except"
            << " fiber_id=" << cur->getId()
            << std::endl
            << shiosylar::BacktraceToString();
    }

    auto raw_ptr = cur.get();
    cur.reset();
    raw_ptr->back(); // 回到当前线程的主协程
    ASSERT2(false, "never reach fiber_id=" + std::to_string(raw_ptr->getId()));

}

} // namespace shiosylar end