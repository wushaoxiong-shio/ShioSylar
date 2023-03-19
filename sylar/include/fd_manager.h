// 文件句柄管理类

#ifndef __FD_MANAGER_H__
#define __FD_MANAGER_H__

#include <memory>
#include <vector>
#include "thread.h"
#include "singleton.h"

namespace shiosylar
{

// 文件句柄上下文类
class FdCtx : public std::enable_shared_from_this<FdCtx>
{
public:
    typedef std::shared_ptr<FdCtx> ptr;
    
    FdCtx(int fd); // 传入一个fd进行初始化
   
    ~FdCtx();

    // 是否已经初始化完成
    bool isInit() const { return m_isInit;}

    // 是否为Socketfd
    bool isSocket() const { return m_isSocket;}

    // 该fd是否已关闭
    bool isClose() const { return m_isClosed;}

    // 设置用户主动设置非阻塞
    void setUserNonblock(bool v) { m_userNonblock = v;}

    // 获取是否用户主动设置的非阻塞
    bool getUserNonblock() const { return m_userNonblock;}

    // 设置系统非阻塞
    void setSysNonblock(bool v) { m_sysNonblock = v;}

    // 获取系统非阻塞
    bool getSysNonblock() const { return m_sysNonblock;}

    // 设置超时时间
    void setTimeout(int type, uint64_t v);

    // 获取超时时间
    uint64_t getTimeout(int type);

private:
    bool init(); // 初始化fd

private:
    bool m_isInit: 1;               // 是否初始化
    bool m_isSocket: 1;             // 是否socket
    bool m_sysNonblock: 1;          // 是否hook非阻塞
    bool m_userNonblock: 1;         // 是否用户主动设置非阻塞
    bool m_isClosed: 1;             // 是否关闭
    int m_fd;                       // 文件句柄
    uint64_t m_recvTimeout;         // 读超时时间毫秒
    uint64_t m_sendTimeout;         // 写超时时间毫秒

}; // class FdCtx end

// 文件句柄管理类
class FdManager
{
public:
    typedef RWMutex RWMutexType;

    FdManager();

    // 获取/创建文件句柄类FdCtx，auto_create标识是否自动创建
    FdCtx::ptr get(int fd, bool auto_create = false);

    // 删除文件句柄类
    void del(int fd);

private:
    RWMutexType m_mutex;                    // 读写锁
    std::vector<FdCtx::ptr> m_datas;        // 文件句柄集合

}; // class FdManager end

// 文件句柄单例
typedef Singleton<FdManager> FdMgr;

} // namespace shiosylar end

#endif
