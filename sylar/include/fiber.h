#ifndef __SHIOSYLAR_FIBER_H__
#define __SHIOSYLAR_FIBER_H__

// 协程

#include <memory>
#include <functional>
#include <ucontext.h>

namespace shiosylar
{

// 协程调度器类
class Scheduler;

// 协程类
class Fiber : public std::enable_shared_from_this<Fiber>
{
friend class Scheduler;
public:
    typedef std::shared_ptr<Fiber> ptr;

    // 协程的运行状态
    enum State
    {
        INIT,   // 初始化状态       
        HOLD,   // 暂停状态
        EXEC,   // 执行中状态       
        TERM,   // 结束状态        
        READY,  // 可执行状态        
        EXCEPT  // 异常状态
    };

private:
    // 私有无参构造，静态调用创建单例主协程，主协程没有运行函数，主协程保存的是当前线程的上下文
    Fiber();

public:
    // 公有构造，传入一个执行函数，创建一个协程并运行函数，use_caller表示任务完成后切回主协程
    Fiber(std::function<void()> cb, size_t stacksize = 0, bool use_caller = false);

    ~Fiber();

    //重置协程，并重置状态
    void reset(std::function<void()> cb);

    //切换到当前协程执行
    void swapIn();

    //切换到后台执行
    void swapOut();

    // 切出到子协程
    void call();

    // 切回到主协程
    void back();

    // 获取当前对象的协程ID
    uint64_t getId() const { return m_id; }

    // 获取当前协程对象的运行状态
    State getState() const { return m_state; }

public:
    //设置当前协程为正在运行的协程
    static void SetThis(Fiber* f);

    // 获取当前正在运行的协程
    static Fiber::ptr GetThis();

    //协程切换到后台，并且设置为Ready状态
    static void YieldToReady();

    //协程切换到后台，并且设置为Hold状态
    static void YieldToHold();

    // 获取总协程数
    static uint64_t TotalFibers();

    // 协程运行的主函数
    static void MainFunc();

    // 使用use_caller时，协程的运行函数
    static void CallerMainFunc();

    // 获取当前正在运行的协程ID
    static uint64_t GetFiberId();

private:
    uint64_t m_id = 0;              // 协程id
    uint32_t m_stacksize = 0;       // 协程运行栈大小
    State m_state = INIT;           // 协程状态
    ucontext_t m_ctx;               // 协程上下文
    void* m_stack = nullptr;        // 协程运行栈指针
    std::function<void()> m_cb;     // 协程运行函数

}; // class Fiber end

} // namespace shiosylar end

#endif
