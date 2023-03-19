#include "../include/logger.h"
#include "../include/config.h"


namespace shiosylar
{

// 日志等级转字符串
const char* LogLevel::ToString(LogLevel::Level level)
{
    switch(level)
    {
        case LogLevel::DEBUG : { return "DEBUG"; break; }
        case LogLevel::INFO : { return "INFO"; break; }
        case LogLevel::WARN : { return "WARN"; break; }
        case LogLevel::ERROR : { return "ERROR"; break; }
        case LogLevel::FATAL : { return "FATAL"; break; }
        default : { return "UNKNOW"; }
    }
    return "UNKNOW";
}

// 字符串转日志等级
LogLevel::Level LogLevel::FromString(const std::string& str)
{
#define XX(level, v) \
    if(str == #v) \
    { \
        return LogLevel::level; \
    }
    XX(DEBUG, debug);
    XX(INFO, info);
    XX(WARN, warn);
    XX(ERROR, error);
    XX(FATAL, fatal);

    XX(DEBUG, DEBUG);
    XX(INFO, INFO);
    XX(WARN, WARN);
    XX(ERROR, ERROR);
    XX(FATAL, FATAL);
    return LogLevel::UNKNOW;
#undef XX
}

LogEvent::LogEvent(std::shared_ptr<Logger> logger,
                    LogLevel::Level level,
                    const char* file,
                    int32_t line,
                    uint32_t elapse,
                    uint32_t thread_id,
                    uint32_t fiber_id,
                    uint64_t time,
                    const std::string& thread_name)
            :
            l_file(file),
            l_line(line),
            l_elapse(elapse),
            l_threadId(thread_id),
            l_fiberId(fiber_id),
            l_time(time),
            l_threadName(thread_name),
            l_logger(logger),
            l_level(level)
{

}

// 设置格式化的字符串，对下面函数的封装
void LogEvent::format(const char* fmt, ...)
{
    va_list al;
    va_start(al, fmt);
    format(fmt, al);
    va_end(al);
}

// 设置格式化的字符串
void LogEvent::format(const char* fmt, va_list al)
{
    char* buf = nullptr;
    int len = vasprintf(&buf, fmt, al);
    if(len != -1)
    {
        l_ss << std::string(buf, len);
        free(buf);
    }
}

LogEventWrap::LogEventWrap(LogEvent::ptr e) : l_event(e)
{

}

// LogEventWrap析构的时候向流输出日志
LogEventWrap::~LogEventWrap()
{
    l_event->getLogger()->log(l_event->getLevel(), l_event);
}

// 获取l_event的字符流对象
std::stringstream& LogEventWrap::getSS()
{
    return l_event->getSS();
}

// 构造函数中对字符串进行解析
LogFormatter::LogFormatter(const std::string& pattern) : l_pattern(pattern)
{
    init();
}

// 向标准输出打印日志
std::string LogFormatter::format(std::shared_ptr<Logger> logger,
                                    LogLevel::Level level,
                                    LogEvent::ptr event)
{
    std::stringstream ss;
    for(auto l : l_items)
        l->format(ss,logger,level,event);
    return ss.str();
}

// 向文件流输出日志
std::ostream& LogFormatter::format(std::ostream& ofs,
                                    std::shared_ptr<Logger> logger,
                                    LogLevel::Level level,
                                    LogEvent::ptr event)
{
    for(auto& i : l_items)
        i->format(ofs, logger, level, event);
    return ofs;
}

// 解析字符串形式的日志格式
void LogFormatter::init()
{
    // vec<string,string,int>，日志字段标识-额外参数-（0为用户消息1为日志字段的输出函数）
    std::vector<std::tuple<std::string, std::string, int>> vec;
    std::string nstr;
    for(size_t i=0; i<l_pattern.size(); ++i)
    {
        if(l_pattern[i] != '%') // 遇到 '%' 才开始往下解析
        {
           nstr.append(1, l_pattern[i]);
           continue;
        }

        if((i+1) < l_pattern.size()) // 判断是否为连续的 '%'
        {
            if(l_pattern[i+1] == '%')
            {
                nstr.append(1,'%');
                continue;
            }
        }

        size_t n = i + 1;
        int fmt_status = 0;
        size_t fmt_begin = 0;
        std::string str = "";
        std::string fmt = "";
        
        while(n < l_pattern.size()) // 解析 '{ }'
        {
            if(!fmt_status && !isalpha(l_pattern[n]) && l_pattern[n] != '{' && l_pattern[n] != '}')
                break;
            if(fmt_status == 0)
            {
                if(l_pattern[n] == '{')
                {
                    str = l_pattern.substr(i+1,n-i-1);
                    fmt_status = 1;
                    fmt_begin = n;
                    ++n;
                    continue;
                }
            }
            if(fmt_status == 1)
            {
                if(l_pattern[n] == '}')
                {
                    fmt = l_pattern.substr(fmt_begin+1,n-fmt_begin-1);
                    fmt_status = 2;
                    break;
                }
            }
            ++n;
        }
        
        if(fmt_status == 0) // 字符串中没有 '{}' 字符
        {
            if(!nstr.empty())
            {
                vec.push_back(std::make_tuple(nstr,std::string(),0));
                nstr.clear();
            }
            
            str = l_pattern.substr(i+1,n-i-1);
            vec.push_back(std::make_tuple(str,fmt,1));
            i = n - 1;
        }
        else if(fmt_status == 1) // 解析出错
        {
            std::cout << "pattern parse error:" << l_pattern << " - "
                        << l_pattern.substr(i) << std::endl;
            vec.push_back(std::make_tuple("<<pattern_error>>",fmt,0));
        }
        else if(fmt_status == 2) // 字符串中有 '{}' 字符
        {
            if(!nstr.empty())
            {
                vec.push_back(std::make_tuple(nstr,std::string(),0));
                nstr.clear();
            }
            vec.push_back(std::make_tuple(str,fmt,1));
            i = n;
        }
    }
    if(!nstr.empty())
        vec.push_back(std::make_tuple(nstr,std::string(),0));
    
    // 函数容器
    static std::map<std::string,std::function<FormatItem::ptr(const std::string& str)>>
        l_format_item =
    {
    #define XX(str, C) \
        {#str, [](const std::string& fmt) { return FormatItem::ptr(new C(fmt)); } }

        XX(m, MessageFormatItem),
        XX(p, LevelFormatItem),
        XX(r, ElapseFormatItem),
        XX(c, NameFormatItem),
        XX(t, ThreadIdFormatItem),
        XX(n, NewLineFormatItem),
        XX(d, DateTimeFormatItem),
        XX(f, FilenameFormatItem),
        XX(l, LineFormatItem),
        XX(T, TabFormatItem),
        XX(F, FiberIdFormatItem),
        XX(N, ThreadNameFormatItem),
    #undef XX
    };

    for(auto& v : vec)
    {
        if(std::get<2>(v) == 0) // 向函数容器插入消息字段
            l_items.push_back(FormatItem::ptr(new StringFormatItem(std::get<0>(v))));
        else
        {
            auto it = l_format_item.find(std::get<0>(v));
            if(it == l_format_item.end())
            {
                l_items.push_back(
                    FormatItem::ptr(new StringFormatItem("<<error_format %"+ std::get<0>(v)+">>")));
                l_error = true;
            }
            else // 向函数容器插入其他字段
                l_items.push_back(it->second(std::get<1>(v)));
        }
    }
}

// 设置日志格式器
void LogAppender::setFormatter(LogFormatter::ptr val)
{
    MutexType::Lock lock(m_mutex);
    l_formatter = val;
    if(l_formatter)
        l_hasFormatter = true;
    else
        l_hasFormatter = false;
}

// 获取日志格式器
LogFormatter::ptr LogAppender::getFormatter()
{
    MutexType::Lock lock(m_mutex);
    return l_formatter;
}

// 初始化设置一个简单的格式器
Logger::Logger(const std::string& name) : l_name(name), l_level(LogLevel::DEBUG), l_formatter(nullptr)
{
    // l_formatter.reset(new LogFormatter("%d{%Y-%m-%d %H:%M:%S}%T%t%T%F%T[%p]%T[%c]%T%f:%l%T%m%n"));
    l_formatter.reset(new LogFormatter("%d{%Y-%m-%d %H:%M:%S}%T%t%T%N%T%m%n"));
}

// 轮流通过输出器输出日志
void Logger::log(LogLevel::Level level, LogEvent::ptr event)
{
    if(level >= l_level)
    {
        auto self = shared_from_this();
        MutexType::Lock lock(m_mutex);
        if(!l_appenders.empty()) // 如果当前日志器没有输出器，则交给主日志器来输出
        {
            for(auto& i : l_appenders)
                i->log(self, level, event);
        }
        else if(l_root)
            l_root->log(level, event);
    }
}

void Logger::debug(LogEvent::ptr event)
{
    log(LogLevel::DEBUG, event);
}

void Logger::info(LogEvent::ptr event)
{
    log(LogLevel::INFO, event);
}

void Logger::warn(LogEvent::ptr event)
{
    log(LogLevel::WARN, event);
}

void Logger::error(LogEvent::ptr event)
{
    log(LogLevel::ERROR, event);
}

void Logger::fatal(LogEvent::ptr event)
{
    log(LogLevel::FATAL, event);
}

// 添加一个日志输出器
void Logger::addAppender(LogAppender::ptr appender)
{
    MutexType::Lock lock(m_mutex);
    if(!appender->getFormatter()) // 如果该输出器没有格式器，则将日志器的格式器赋给该输出器
    {
        MutexType::Lock ll(appender->m_mutex);
        appender->l_formatter = l_formatter;
    }
    l_appenders.push_back(appender);
}

// 删除一个日志输出器
void Logger::delAppender(LogAppender::ptr appender)
{
    MutexType::Lock lock(m_mutex);
    for(auto it = l_appenders.begin(); it != l_appenders.end(); ++it)
    {
        if(*it == appender)
        {
            l_appenders.erase(it);
            break;
        }
    }
}

// 设置日志格式器，传LogFormatter::ptr
void Logger::setFormatter(LogFormatter::ptr val)
{
    MutexType::Lock lock(m_mutex);
    l_formatter = val;
    for(auto& i : l_appenders)
    {
        MutexType::Lock ll(i->m_mutex);
        if(!i->l_hasFormatter) // 如果该输出器没有格式器，则将日志器的格式器赋给该输出器
            i->l_formatter = l_formatter;
    }
}

// 设置日志格式器，传string&
void Logger::setFormatter(const std::string& val)
{
    // 用val构造一个LogFormatter，然后设置格式器
    shiosylar::LogFormatter::ptr new_val(new shiosylar::LogFormatter(val));
    if(new_val->isError())
    {
        std::cout << "Logger setFormatter name=" << l_name
                  << " value=" << val << " invalid formatter"
                  << std::endl;
        return;
    }
    l_formatter = new_val;
    setFormatter(new_val);
}

// 将日志器信息转成 yaml 格式的字符串
std::string Logger::toYamlString()
{
    MutexType::Lock lock(m_mutex);
    YAML::Node node;
    node["name"] = l_name;
    if(l_level != LogLevel::UNKNOW)
        node["level"] = LogLevel::ToString(l_level);
    if(l_formatter)
        node["formatter"] = l_formatter->getPattern();
    for(auto& i : l_appenders)
        node["appenders"].push_back(YAML::Load(i->toYamlString()));
    std::stringstream ss;
    ss << node;
    return ss.str();
}

// 获取日志格式器
LogFormatter::ptr Logger::getFormatter()
{
    MutexType::Lock lock(m_mutex);
    return l_formatter;
}

// 清空所有的日志输出器
void Logger::clearAppenders()
{
    MutexType::Lock lock(m_mutex);
    l_appenders.clear();
}

// 调用格式器的输出函数，输出日志
void StdOutLogAppender::log(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event)
{
    if(level >= l_level)
    {
        MutexType::Lock lock(m_mutex);
        std::cout << l_formatter->format(logger,level,event);
    }
}

// 将输出器的信息转成 yaml 格式的字符串
std::string StdOutLogAppender::toYamlString()
{
    MutexType::Lock lock(m_mutex);
    YAML::Node node;
    node["type"] = "StdoutLogAppender";
    if(l_level != LogLevel::UNKNOW) {
        node["level"] = LogLevel::ToString(l_level);
    }
    if(l_hasFormatter && l_formatter) {
        node["formatter"] = l_formatter->getPattern();
    }
    std::stringstream ss;
    ss << node;
    return ss.str();
}

FileLogAppender::FileLogAppender(const std::string& filename) : l_filename(filename)
{
    reopen();
}

// 调用格式器的输出函数，输出日志
void FileLogAppender::log(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event)
{
    if(level >= l_level)
    {
        uint64_t now = event->getTime();
        if(now >= (l_lastTime + 3)) // 每过三秒重新打开一次文件，防止文件不存在导致输出失败
        {
            reopen();
            l_lastTime = now;
        }
        MutexType::Lock lock(m_mutex);
        if(!l_formatter->format(l_filestream, logger, level, event))
            std::cout << "error" << std::endl;
    }
}

// 将输出器的信息转成 yaml 格式的字符串
std::string FileLogAppender::toYamlString()
{
    MutexType::Lock lock(m_mutex);
    YAML::Node node;
    node["type"] = "FileLogAppender";
    node["file"] = l_filename;
    if(l_level != LogLevel::UNKNOW)
        node["level"] = LogLevel::ToString(l_level);
    if(l_hasFormatter && l_formatter)
        node["formatter"] = l_formatter->getPattern();
    std::stringstream ss;
    ss << node;
    return ss.str();
}

// 重新打开文件，确保能够成功输出日志
bool FileLogAppender::reopen()
{
    MutexType::Lock lock(m_mutex);
    if(l_filestream)
        l_filestream.close();
    return FSUtil::OpenForWrite(l_filestream, l_filename, std::ios::app);
}

// 构造函数创建一个主日志器，默认输出器为标准输出
LoggerManager::LoggerManager()
{
    l_root.reset(new Logger);
    l_root->addAppender(LogAppender::ptr(new StdOutLogAppender));
    l_loggers[l_root->l_name] = l_root;
}

// 通过名称查询并返回日志器，未查到则创建一个再返回
Logger::ptr LoggerManager::getLogger(const std::string& name)
{
    MutexType::Lock lock(m_mutex);
    auto it = l_loggers.find(name);
    if(it != l_loggers.end())
        return it->second;

    Logger::ptr logger(new Logger(name));
    logger->l_root = l_root;
    l_loggers[name] = logger;
    return logger;
}

// yaml日志输出类
struct LogAppenderDefine
{
    int type = 0; //1 FileLogAppender, 2 StdOutLogAppender
    LogLevel::Level level = LogLevel::UNKNOW;   // 日志等级
    std::string formatter;                      // 日志格式器-字符串形式
    std::string file;                           // 输出文件名

    bool operator==(const LogAppenderDefine& oth) const
    {
        return type == oth.type
            && level == oth.level
            && formatter == oth.formatter
            && file == oth.file;
    }
};

// yaml日志器类
struct LogDefine
{
    std::string name;                               // 日志器名称
    LogLevel::Level level = LogLevel::UNKNOW;       // 日志等级
    std::string formatter;                          // 日志格式器-字符串形式
    std::vector<LogAppenderDefine> appenders;       // 输出器容器

    bool operator==(const LogDefine& oth) const
    {
        return name == oth.name
            && level == oth.level
            && formatter == oth.formatter
            && appenders == appenders;
    }

    bool operator<(const LogDefine& oth) const
    {
        return name < oth.name;
    }

    bool isValid() const // 日志器名称是否有效，不为空
    {
        return !name.empty();
    }
};

// 模板类 LogDefine 转 string 的范式类
template<>
class LexicalCast<std::string, LogDefine>
{
public:
    LogDefine operator()(const std::string& v)
    {
        YAML::Node n = YAML::Load(v);
        LogDefine ld;
        if(!n["name"].IsDefined())
        {
            std::cout << "log config error: name is null, " << n << std::endl;
            throw std::logic_error("log config name is null");
        }
        ld.name = n["name"].as<std::string>();
        ld.level = LogLevel::FromString(n["level"].IsDefined() ? n["level"].as<std::string>() : "");
        if(n["formatter"].IsDefined())
            ld.formatter = n["formatter"].as<std::string>();
        if(n["appenders"].IsDefined())
        {
            //std::cout << "==" << ld.name << " = " << n["appenders"].size() << std::endl;
            for(size_t x = 0; x < n["appenders"].size(); ++x)
            {
                auto a = n["appenders"][x];
                if(!a["type"].IsDefined())
                {
                    std::cout << "log config error: appender type is null, " << a
                              << std::endl;
                    continue;
                }
                std::string type = a["type"].as<std::string>();
                LogAppenderDefine lad;
                if(type == "FileLogAppender")
                {
                    lad.type = 1;
                    if(!a["file"].IsDefined())
                    {
                        std::cout << "log config error: fileappender file is null, " << a
                              << std::endl;
                        continue;
                    }
                    lad.file = a["file"].as<std::string>();
                    if(a["formatter"].IsDefined())
                        lad.formatter = a["formatter"].as<std::string>();
                }
                else if(type == "StdoutLogAppender")
                {
                    lad.type = 2;
                    if(a["formatter"].IsDefined())
                        lad.formatter = a["formatter"].as<std::string>();
                }
                else
                {
                    std::cout << "log config error: appender type is invalid, " << a
                              << std::endl;
                    continue;
                }
                ld.appenders.push_back(lad);
            }
        }
        return ld;
    }
};

// 模板类 string 转 LogDefine 的范式类
template<>
class LexicalCast<LogDefine, std::string>
{
public:
    std::string operator()(const LogDefine& i)
    {
        YAML::Node n;
        n["name"] = i.name;
        if(i.level != LogLevel::UNKNOW)
            n["level"] = LogLevel::ToString(i.level);
        if(!i.formatter.empty())
            n["formatter"] = i.formatter;
        for(auto& a : i.appenders)
        {
            YAML::Node na;
            if(a.type == 1)
            {
                na["type"] = "FileLogAppender";
                na["file"] = a.file;
            }
            else if(a.type == 2)
                na["type"] = "StdoutLogAppender";
            if(a.level != LogLevel::UNKNOW)
                na["level"] = LogLevel::ToString(a.level);
            if(!a.formatter.empty())
                na["formatter"] = a.formatter;
            n["appenders"].push_back(na);
        }
        std::stringstream ss;
        ss << n;
        return ss.str();
    }
};

// 定义一个全局变量的日志器对象，名称为logs
shiosylar::ConfigVar<std::set<LogDefine> >::ptr g_log_defines =
    shiosylar::Config::Lookup("logs", std::set<LogDefine>(), "logs config");

// 向配置器添加一个日志配置的回调函数
struct LogIniter
{
    LogIniter()
    {
        // 添加回调函数，将配置文件中的设置同步到当前的环境中
        g_log_defines->addListener
        (
            [](const std::set<LogDefine>& old_value,
                    const std::set<LogDefine>& new_value)
            {
                LOG_INFO(LOG_ROOT()) << "on_logger_conf_changed";
                for(auto& i : new_value)
                {
                    auto it = old_value.find(i);
                    shiosylar::Logger::ptr logger;
                    if(it == old_value.end())
                        //新增logger
                        logger = LOG_NAME(i.name);
                    else 
                    {
                        if(!(i == *it))
                            //修改的logger
                            logger = LOG_NAME(i.name);
                        else
                            continue;
                    }
                    logger->setLevel(i.level);
                    //std::cout << "** " << i.name << " level=" << i.level
                    //<< "  " << logger << std::endl;
                    if(!i.formatter.empty())
                        logger->setFormatter(i.formatter);
                    logger->clearAppenders();
                    for(auto& a : i.appenders)
                    {
                        shiosylar::LogAppender::ptr ap;
                        if(a.type == 1)
                            ap.reset(new FileLogAppender(a.file));
                        else if(a.type == 2)
                        {
                            // if(!shiosylar::EnvMgr::GetInstance()->has("d"))
                            ap.reset(new StdOutLogAppender);
                            // else
                            //     continue;
                        }
                        ap->setLevel(a.level);
                        if(!a.formatter.empty())
                        {
                            LogFormatter::ptr fmt(new LogFormatter(a.formatter));
                            if(!fmt->isError())
                                ap->setFormatter(fmt);
                            else
                                std::cout << "log.name=" << i.name << " appender type=" << a.type
                                        << " formatter=" << a.formatter << " is invalid" << std::endl;
                        }
                        logger->addAppender(ap);
                    }
                }

                for(auto& i : old_value)
                {
                    auto it = new_value.find(i);
                    if(it == new_value.end())
                    {
                        //删除logger
                        auto logger = LOG_NAME(i.name);
                        logger->setLevel((LogLevel::Level)0);
                        logger->clearAppenders();
                    }
                }
            }
        );
    }
};

// 全局变量，确保初始化就注册日志回调函数
static LogIniter __log_init;

// 依次将所有的日志器转成 yaml 格式的字符串
std::string LoggerManager::toYamlString()
{
    MutexType::Lock lock(m_mutex);
    YAML::Node node;
    for(auto& i : l_loggers)
        node.push_back(YAML::Load(i.second->toYamlString()));
    std::stringstream ss;
    ss << node;
    return ss.str();
}


} // namespace shiosylar end