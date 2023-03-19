#ifndef __SHIOSYLAR_LOGGER_H__
#define __SHIOSYLAR_LOGGER_H__

// 日志模块 LogEvent->Logger->LogAppender->LogFmatter

#include "util.h"
#include "singleton.h"
#include "thread.h"

#include <string>
#include <stdint.h>
#include <memory>
#include <list>
#include <vector>
#include <map>
#include <iostream>
#include <fstream>
#include <sstream>
#include <functional>
#include <time.h>
#include <string.h>
#include <stdarg.h>

// SYLAR_LOG_LEVEL 创建一个临时的LogEventWrap对象，它内含一个LogEvent::ptr随着LogEventWrap
// 而创建，LogEventWrap析构函数调用logger对象输出日志，输出完就释放资源（临时对象）
#define SYLAR_LOG_LEVEL(logger, level) \
    if(logger->getLevel() <= level) \
        shiosylar::LogEventWrap( \
            shiosylar::LogEvent::ptr( \
                new shiosylar::LogEvent( \
                                        logger, \
                                        level,  \
                                        __FILE__, \
                                        __LINE__, \
                                        0, \
                                        shiosylar::GetThreadId(),\
                                        shiosylar::GetFiberId(), \
                                        time(0), \
                                        shiosylar::Thread::GetName() \
                                        ) \
                                    )\
                                ) \
        .getSS()

#define LOG_DEBUG(logger) SYLAR_LOG_LEVEL(logger, shiosylar::LogLevel::DEBUG)
#define LOG_INFO(logger) SYLAR_LOG_LEVEL(logger, shiosylar::LogLevel::INFO)
#define LOG_WARN(logger) SYLAR_LOG_LEVEL(logger, shiosylar::LogLevel::WARN)
#define LOG_ERROR(logger) SYLAR_LOG_LEVEL(logger, shiosylar::LogLevel::ERROR)
#define LOG_FATAL(logger) SYLAR_LOG_LEVEL(logger, shiosylar::LogLevel::FATAL)

// SYLAR_LOG_FMT_LEVEL同上，只是增加了自定义的格式字符串
#define SYLAR_LOG_FMT_LEVEL(logger, level, fmt, ...) \
    if(logger->getLevel() <= level) \
        shiosylar::LogEventWrap( \
            shiosylar::LogEvent::ptr( \
                new shiosylar::LogEvent( \
                                        logger, \
                                        level, \
                                        __FILE__, \
                                        __LINE__, \
                                        0, \
                                        shiosylar::GetThreadId(),\
                                        shiosylar::GetFiberId(), \
                                        time(0), \
                                        shiosylar::Thread::GetName() \
                                        ) \
                                    ) \
                                ) \
        .getEvent()->format(fmt, __VA_ARGS__)

#define LOG_FMT_DEBUG(logger, fmt, ...) SYLAR_LOG_FMT_LEVEL(logger, shiosylar::LogLevel::DEBUG, fmt, __VA_ARGS__)
#define LOG_FMT_INFO(logger, fmt, ...)  SYLAR_LOG_FMT_LEVEL(logger, shiosylar::LogLevel::INFO, fmt, __VA_ARGS__)
#define LOG_FMT_WARN(logger, fmt, ...)  SYLAR_LOG_FMT_LEVEL(logger, shiosylar::LogLevel::WARN, fmt, __VA_ARGS__)
#define LOG_FMT_ERROR(logger, fmt, ...) SYLAR_LOG_FMT_LEVEL(logger, shiosylar::LogLevel::ERROR, fmt, __VA_ARGS__)
#define LOG_FMT_FATAL(logger, fmt, ...) SYLAR_LOG_FMT_LEVEL(logger, shiosylar::LogLevel::FATAL, fmt, __VA_ARGS__)

// 返回主日志器对象，LoggerMgr是单例对象，通过静态方法获取日志管理器类
#define LOG_ROOT() shiosylar::LoggerMgr::GetInstance()->getRoot()

// 根据名字查找并返回日志器对象，如果没找到，则创建一个再返回
#define LOG_NAME(name) shiosylar::LoggerMgr::GetInstance()->getLogger(name)

namespace shiosylar
{

class Logger;
class LoggerManager;

// 日志等级枚举
class LogLevel
{
public:
    enum Level
    {
        UNKNOW = 0,
        DEBUG = 1,
        INFO = 2,
        WARN = 3,
        ERROR = 4,
        FATAL = 5
    };

    // 日志等级转字符串
    static const char* ToString(LogLevel::Level level);
    
    // 字符串转日志等级
    static LogLevel::Level FromString(const std::string& str);

}; // class LogLevel end


// 日志事件类
class LogEvent 
{
public:
	typedef std::shared_ptr<LogEvent> ptr;

	LogEvent(std::shared_ptr<Logger> logger,
                LogLevel::Level level,
                const char* file,
                int32_t line,
                uint32_t elapse,
                uint32_t thread_id,
                uint32_t fiber_id,
                uint64_t time,
                const std::string& thread_name);

    // 获取文件名
    const char* getFile() const { return l_file; }

    // 获取行号
    int32_t getLine() const { return l_line; }

    // 获取时间戳
    uint32_t getElapse() const { return l_elapse; }

    // 获取线程ID
    uint32_t getThreadId() const { return l_threadId; }

    // 获取协程ID
    uint32_t getFiberId() const { return l_fiberId; }

    // 获取时间
    uint64_t getTime() const { return l_time; }

    // 获取线程名
    const std::string& getThreadName() const { return l_threadName;}

    // 获取日志内容
    std::string getContent() const { return l_ss.str(); }

    // 获取所属的日志器
    std::shared_ptr<Logger> getLogger() const { return l_logger;}

    // 获取日志等级
    LogLevel::Level getLevel() const { return l_level;}

    // 获取字符流对象
    std::stringstream& getSS() { return l_ss; }

    // 设置格式化的字符串，对下面函数的封装
    void format(const char* fmt, ...);

    // 设置格式化的字符串   
    void format(const char* fmt, va_list al);

private:
	const char* l_file = nullptr;       // 文件名
	int32_t l_line = 0;                 // 代码行号
    uint32_t l_elapse =0;               // 服务器启动到现在的毫秒数
	uint32_t l_threadId = 0;            // 线程ID
    uint32_t l_fiberId = 0;             // 协程ID
	uint64_t l_time =0;                 // 时间戳
    std::string l_threadName;           // 线程名
	std::stringstream l_ss;             // 字符流对象，用来接收存储用户的消息
    std::shared_ptr<Logger> l_logger;   // 所属的logger对象
    LogLevel::Level l_level;            // 日志等级

}; // class LogEvent end


// 日志事件对象的上层包装，RAII机制配合宏定义快捷输出日志
class LogEventWrap
{
public:
    LogEventWrap(LogEvent::ptr e); // 构造函数获取资源并初始化

    ~LogEventWrap(); // 析构函数内执行log()函数输出日志，释放资源

    // 获取内含的日志事件对象
    LogEvent::ptr getEvent() const { return l_event;}

    // 调用l_event的getSS()
    std::stringstream& getSS();

private:
    LogEvent::ptr l_event; // 内含一个LogEvent::ptr

}; // class LogEventWrap end

// 日志格式器
class LogFormatter
{
public:
    typedef std::shared_ptr<LogFormatter> ptr;

    LogFormatter(const std::string& pattern);

    // 输出到标准输出
    std::string format(std::shared_ptr<Logger> logger,
                        LogLevel::Level level,
                        LogEvent::ptr event);

    // 输出到文件，需要传入一个文件流
    std::ostream& format(std::ostream& ofs,
                            std::shared_ptr<Logger> logger,
                            LogLevel::Level level,
                            LogEvent::ptr event);

public:
    void init(); // 根据字符串的日志格式 解析出 输出的日志格式

    // 查询日志格式是否正确
    bool isError() const { return l_error; }

    // 返回字符串形式的日志格式
    const std::string getPattern() const { return l_pattern; }

    // 内嵌类-不同日志字段的顶层类
    class FormatItem
    {
    public:
        typedef std::shared_ptr<FormatItem> ptr;

        virtual ~FormatItem() { }

        // 将日志字段输出到 ostream流中，stringstream继承于ostream
        // 所以传参设置为ostream，方便兼容子类的对象
        virtual void format(std::ostream& os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) = 0;

    }; // class FormatItem end

private:
    std::string l_pattern;                  // 字符串形式的日志格式
    std::vector<FormatItem::ptr> l_items;   // 已经解析并排好的日志字段，函数容器
    bool l_error = false;                   // 日志格式是否正确

}; // class LogFormatter end

// 日志输出器--顶层类
class LogAppender
{
public:
    friend class Logger;

	typedef std::shared_ptr<LogAppender> ptr;

    typedef Spinlock MutexType;

    virtual ~LogAppender() {};

    // 通过日志格式器向日志流输出日志
    virtual void log(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) = 0;

    virtual std::string toYamlString() = 0;

    // 设置日志格式器
    void setFormatter(LogFormatter::ptr val);

    // 获取日志格式器
    LogFormatter::ptr getFormatter();

    // 获取日志等级
    LogLevel::Level getLevel() const { return l_level; }

    // 设置日志等级
    void setLevel(LogLevel::Level val) { l_level = val;}

protected:
    LogLevel::Level l_level = LogLevel::DEBUG;      // 日志等级
    bool l_hasFormatter = false;                    // 是否已存在日志格式
    MutexType m_mutex;                              // 互斥锁
    LogFormatter::ptr l_formatter;                  // 日志格式器

}; // class LogAppender end

// 日志器类
class Logger : public std::enable_shared_from_this<Logger>
{
public:
    friend class LoggerManager;

	typedef std::shared_ptr<Logger> ptr;

    typedef Spinlock MutexType;

	Logger(const std::string& name = "root");

    // 输出日志
	void log(LogLevel::Level level, LogEvent::ptr event);

    void debug(LogEvent::ptr event);

    void info(LogEvent::ptr event);

    void warn(LogEvent::ptr event);

    void error(LogEvent::ptr event);

    void fatal(LogEvent::ptr event);

    // 添加一个日志输出器
    void addAppender(LogAppender::ptr appender);

    // 删除一个日志输出器
    void delAppender(LogAppender::ptr appender);

    // 清空所有的日志输出器
    void clearAppenders();

    // 获取日志等级
    LogLevel::Level getLevel() const { return l_level; }
    
    // 设置日志等级
    void setLevel(LogLevel::Level level) { l_level = level; }

    // 获取日志器的名称
    const std::string getName() const { return l_name; }

    // 设置日志格式器，传LogFormatter::ptr
    void setFormatter(LogFormatter::ptr val);

    // 设置日志格式器，传string&
    void setFormatter(const std::string& val);

    // 获取日志格式器
    LogFormatter::ptr getFormatter();

    // 将日志器的信息转成 yaml 格式的字符串
    std::string toYamlString();

private:
	std::string l_name;                                 // 日志名称
    LogLevel::Level l_level;                            // 日志级别
    MutexType m_mutex;                                  // 互斥锁
	std::list<LogAppender::ptr> l_appenders;            // 日志Appender集合
    LogFormatter::ptr l_formatter;                      // 日志格式器
    Logger::ptr l_root;                                 // 主日志器的指针

}; // class Logger end

// 日志字段--输出消息
class MessageFormatItem : public LogFormatter::FormatItem
{
public:
    MessageFormatItem(const std::string& str = "") { }

    void format(std::ostream& os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override
    {
        os << event->getContent();
    }

}; // class MessageFormatItem end

// 日志字段--输出日志等级
class LevelFormatItem : public LogFormatter::FormatItem
{
public:
    LevelFormatItem(const std::string& str = "") { }

    void format(std::ostream& os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override
    {
        os << LogLevel::ToString(level);
    }

}; // class LevelFormatItem end

// 日志字段--输出时间戳
class ElapseFormatItem : public LogFormatter::FormatItem
{
public:
    ElapseFormatItem(const std::string& str = "") { }

    void format(std::ostream& os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override
    {
        os << event->getElapse();
    }

}; // class ElapseFormatItem end

// 日志字段--输出日志器的名称
class NameFormatItem : public LogFormatter::FormatItem
{
public:
    NameFormatItem(const std::string& str = "") { }

    void format(std::ostream& os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override
    {
        os << event->getLogger()->getName();
    }

}; // class NameFormatItem end

// 日志字段--输出线程ID
class ThreadIdFormatItem : public LogFormatter::FormatItem
{
public:
    ThreadIdFormatItem(const std::string& str = "") { }

    void format(std::ostream& os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override
    {
        os << event->getThreadId();
    }

}; // class ThreadIdFormatItem end

// 日志字段--输出协程ID
class FiberIdFormatItem : public LogFormatter::FormatItem
{
public:
    FiberIdFormatItem(const std::string& str = "") { }

    void format(std::ostream& os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override
    {
        os << event->getFiberId();
    }

}; // class FiberIdFormatItem end

// 日志字段--输出线程名称
class ThreadNameFormatItem : public LogFormatter::FormatItem
{
public:
    ThreadNameFormatItem(const std::string& str = "") {}
    void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override
    {
        os << event->getThreadName();
    }
};

// 日志字段--输出日期时间
class DateTimeFormatItem : public LogFormatter::FormatItem
{
public:
    DateTimeFormatItem(const std::string& format = "%Y-%m-%d %H:%M%S") : l_format(format)
    {
        if(l_format.empty())
            l_format = "%Y-%m-%d %H:%M:%S";
    }

    void format(std::ostream& os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override
    {
        struct tm tm;
        time_t time = event->getTime();
        localtime_r(&time, &tm);
        char buf[64];
        strftime(buf, sizeof(buf), l_format.c_str(), &tm);
        os << buf;
    }

private:
    std::string l_format;

}; // class DateTimeFormatItem end

// 日志字段--输出文件名
class FilenameFormatItem : public LogFormatter::FormatItem
{
public:
    FilenameFormatItem(const std::string& str = "") { }

    void format(std::ostream& os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override
    {
        os << event->getFile();
    }

}; // class FilenameFormatItem end

// 日志字段--输出行号
class LineFormatItem : public LogFormatter::FormatItem
{
public:
    LineFormatItem(const std::string& str = "") { }

    void format(std::ostream& os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override
    {
        os << event->getLine();
    }

}; // class LineFormatItem end

// 日志字段--输出 '\n'
class NewLineFormatItem : public LogFormatter::FormatItem
{
public:
    NewLineFormatItem(const std::string& str = "") { }

    void format(std::ostream& os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override
    {
        os << std::endl;
    }

}; // class LineFormatItem end

// 日志字段--输出字符串
class StringFormatItem : public LogFormatter::FormatItem
{
public:
    StringFormatItem(const std::string& str) : l_string(str) {}

    void format(std::ostream& os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override
    {
        os << l_string;
    }
private:
    std::string l_string;

}; // class StringFormatItem end

// 日志字段--输出 '\t'
class TabFormatItem : public LogFormatter::FormatItem
{
public:
    TabFormatItem(const std::string& str = "") { }
    
    void format(std::ostream& os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override
    {
        os << "\t";
    }

}; // class TabFormatItem end

// 日志默认输出-stdout标准输出
class StdOutLogAppender : public LogAppender
{
public:
    typedef std::shared_ptr<StdOutLogAppender> ptr;

    // 调用格式器的输出函数，输出日志
    void log(std::shared_ptr<Logger> logger,
                LogLevel::Level level,
                LogEvent::ptr event) override;
    
    // 将输出器的信息转成 yaml 格式的字符串
    std::string toYamlString() override;

}; // class StdOutLogAppender end

// 日志输出到文件
class FileLogAppender : public LogAppender
{
public:
    typedef std::shared_ptr<FileLogAppender> ptr;

    FileLogAppender(const std::string& filename);

    // 调用格式器的输出函数，输出日志
    void log(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override;

    // 将输出器的信息转成 yaml 格式的字符串
    std::string toYamlString() override;

    bool reopen(); // 重新打开文件

private:
    std::string l_filename;         // 日志输出文件名
    std::ofstream l_filestream;     // 文件输出流
    uint64_t l_lastTime = 0;        // 最后一次写入的时间

}; // class FileLogAppender end

// 日志器管理类--全局单例类
class LoggerManager
{
public:
    typedef Spinlock MutexType;
    
    LoggerManager();

    // 通过名称查询并返回日志器，未查到则创建一个再返回
    Logger::ptr getLogger(const std::string& name);

    // 获取主日志器对象
    Logger::ptr getRoot() const { return l_root;}

    // 依次将所有的日志器转成 yaml 格式的字符串
    std::string toYamlString();

private:
    MutexType m_mutex;                                  // 互斥锁
    std::map<std::string, Logger::ptr> l_loggers;       // 日志器容器
    Logger::ptr l_root;                                 // 主日志器

};

/// 日志器管理类单例模式
typedef shiosylar::Singleton<LoggerManager> LoggerMgr;

} // namespace shiosylar end

#endif