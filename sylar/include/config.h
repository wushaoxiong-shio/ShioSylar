#ifndef __SHIOSYLAR_CONFIG_H__
#define __SHIOSYLAR_CONFIG_H__

// 系统配置类

#include "thread.h"
#include "logger.h"
#include "util.h"

#include <memory>
#include <string>
#include <sstream>
#include <boost/lexical_cast.hpp>
#include <yaml-cpp/yaml.h>
#include <vector>
#include <list>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <functional>


namespace shiosylar
{

// 类型转换的基础类，from--to
template<class F, class T>
class LexicalCast
{
public:
    T operator()(const F& v)
    {
        return boost::lexical_cast<T>(v); // boost中一个用于简单类型的转换
    }

}; // class LexicalCast end

// 模板偏特化，从string转vector
template<class T>
class LexicalCast<std::string, std::vector<T> >
{
public:
    std::vector<T> operator()(const std::string& v)
    {
        YAML::Node node = YAML::Load(v);
        typename std::vector<T> vec;
        std::stringstream ss;
        for(size_t i = 0; i < node.size(); ++i)
        {
            ss.str("");
            ss << node[i];
            vec.push_back(LexicalCast<std::string, T>()(ss.str()));
        }
        return vec;
    }

}; // class LexicalCast<std::string,std::vector<T>> end

// 模板偏特化，从vector转string
template<class T>
class LexicalCast<std::vector<T>, std::string>
{
public:
    std::string operator()(const std::vector<T>& v)
    {
        YAML::Node node(YAML::NodeType::Sequence);
        for(auto& i : v)
            node.push_back(YAML::Load(LexicalCast<T, std::string>()(i)));
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
}; // class LexicalCast<std::vector<T>,std::string> end

// 模板偏特化，从string转list
template<class T>
class LexicalCast<std::string, std::list<T> >
{
public:
    std::list<T> operator()(const std::string& v)
    {
        YAML::Node node = YAML::Load(v);
        typename std::list<T> vec;
        std::stringstream ss;
        for(size_t i = 0; i < node.size(); ++i)
        {
            ss.str("");
            ss << node[i];
            vec.push_back(LexicalCast<std::string, T>()(ss.str()));
        }
        return vec;
    }

}; // class LexicalCast<std::string,std::list<T>> end

// 模板偏特化，从list转string
template<class T>
class LexicalCast<std::list<T>, std::string>
{
public:
    std::string operator()(const std::list<T>& v)
    {
        YAML::Node node(YAML::NodeType::Sequence);
        for(auto& i : v)
            node.push_back(YAML::Load(LexicalCast<T, std::string>()(i)));
        std::stringstream ss;
        ss << node;
        return ss.str();
    }

}; // class LexicalCast<std::list<T>,std::string> end

// 模板偏特化，从string转set
template<class T>
class LexicalCast<std::string, std::set<T> >
{
public:
    std::set<T> operator()(const std::string& v)
    {
        YAML::Node node = YAML::Load(v);
        typename std::set<T> vec;
        std::stringstream ss;
        for(size_t i = 0; i < node.size(); ++i)
        {
            ss.str("");
            ss << node[i];
            vec.insert(LexicalCast<std::string, T>()(ss.str()));
        }
        return vec;
    }

}; //class LexicalCast<std::string,std::set<T>> end

// 模板偏特化，从set转string
template<class T>
class LexicalCast<std::set<T>, std::string>
{
public:
    std::string operator()(const std::set<T>& v)
    {
        YAML::Node node(YAML::NodeType::Sequence);
        for(auto& i : v)
            node.push_back(YAML::Load(LexicalCast<T, std::string>()(i)));
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
}; // class LexicalCast<std::set<T>,std::string> end

// 模板偏特化，从string转unordered_set
template<class T>
class LexicalCast<std::string, std::unordered_set<T> >
{
public:
    std::unordered_set<T> operator()(const std::string& v)
    {
        YAML::Node node = YAML::Load(v);
        typename std::unordered_set<T> vec;
        std::stringstream ss;
        for(size_t i = 0; i < node.size(); ++i)
        {
            ss.str("");
            ss << node[i];
            vec.insert(LexicalCast<std::string, T>()(ss.str()));
        }
        return vec;
    }

}; // class LexicalCast<std::string,std::unordered_set<T>> end

// 模板偏特化，从unordered_set转string
template<class T>
class LexicalCast<std::unordered_set<T>, std::string>
{
public:
    std::string operator()(const std::unordered_set<T>& v) 
    {
        YAML::Node node(YAML::NodeType::Sequence);
        for(auto& i : v)
            node.push_back(YAML::Load(LexicalCast<T, std::string>()(i)));
        std::stringstream ss;
        ss << node;
        return ss.str();
    }

}; // class LexicalCast<std::unordered_set<T>,std::string> end

// 模板偏特化，从string转map<string,T>
template<class T>
class LexicalCast<std::string, std::map<std::string, T> >
{
public:
    std::map<std::string, T> operator()(const std::string& v)
    {
        YAML::Node node = YAML::Load(v);
        typename std::map<std::string, T> vec;
        std::stringstream ss;
        for(auto it = node.begin(); it != node.end(); ++it)
        {
            ss.str("");
            ss << it->second;
            vec.insert(std::make_pair(it->first.Scalar(),
                        LexicalCast<std::string, T>()(ss.str())));
        }
        return vec;
    }

}; // class LexicalCast<std::string,std::map<std::string,T>> end

// 模板偏特化，从map<string,T>转string
template<class T>
class LexicalCast<std::map<std::string, T>, std::string>
{
public:
    std::string operator()(const std::map<std::string, T>& v)
    {
        YAML::Node node(YAML::NodeType::Map);
        for(auto& i : v)
            node[i.first] = YAML::Load(LexicalCast<T, std::string>()(i.second));
        std::stringstream ss;
        ss << node;
        return ss.str();
    }

}; // class LexicalCast<std::map<std::string,T>,std::string> end

// 模板偏特化，从string转unordered_map<string,T>
template<class T>
class LexicalCast<std::string, std::unordered_map<std::string, T> >
{
public:
    std::unordered_map<std::string, T> operator()(const std::string& v)
    {
        YAML::Node node = YAML::Load(v);
        typename std::unordered_map<std::string, T> vec;
        std::stringstream ss;
        for(auto it = node.begin(); it != node.end(); ++it)
        {
            ss.str("");
            ss << it->second;
            vec.insert(std::make_pair(it->first.Scalar(),
                        LexicalCast<std::string, T>()(ss.str())));
        }
        return vec;
    }

}; // class LexicalCast<std::string,std::unordered_map<std::string,T> > end

// 模板偏特化，从unordered_map<string,T>转string
template<class T>
class LexicalCast<std::unordered_map<std::string, T>, std::string>
{
public:
    std::string operator()(const std::unordered_map<std::string, T>& v)
    {
        YAML::Node node(YAML::NodeType::Map);
        for(auto& i : v)
            node[i.first] = YAML::Load(LexicalCast<T, std::string>()(i.second));
        std::stringstream ss;
        ss << node;
        return ss.str();
    }

}; // class LexicalCast<std::unordered_map<std::string, T>, std::string> end


// 系统配置的基础类
class ConfigVarBase
{
public:
    typedef std::shared_ptr<ConfigVarBase> ptr;
    
    // 将所有字母转成小写
    ConfigVarBase(const std::string& name, const std::string& description = "")
        : m_name(name), m_description(description)
    {
        std::transform(m_name.begin(), m_name.end(), m_name.begin(), ::tolower);
    }

    virtual ~ConfigVarBase() {}

    // 获取配置参数的名称
    const std::string& getName() const { return m_name;}

    // 获取配置参数的描述
    const std::string& getDescription() const { return m_description;}

    virtual std::string toString() = 0;

    virtual bool fromString(const std::string& val) = 0;

    virtual std::string getTypeName() const = 0;

protected:
    // 配置参数的名称
    std::string m_name;
    // 配置参数的描述
    std::string m_description;

}; // class ConfigVarBase end

// 系统配置的具体类
template<class T,
            class FromStr = LexicalCast<std::string, T>,
            class ToStr = LexicalCast<T, std::string> >
class ConfigVar : public ConfigVarBase
{
public:
    typedef RWMutex RWMutexType;

    typedef std::shared_ptr<ConfigVar> ptr;

    // 当配置文件改变时的回调函数
    typedef std::function<void (const T& old_value, const T& new_value)> on_change_cb;

    ConfigVar(const std::string& name,
                const T& default_value,
                const std::string& description = "")
        :
        ConfigVarBase(name, description),
        m_val(default_value)
    {
        
    }

    // 将配置值转成string返回
    std::string toString() override
    {
        try
        {
            //return boost::lexical_cast<std::string>(m_val);
            RWMutexType::ReadLock lock(m_mutex);
            return ToStr()(m_val);
        }
        catch (std::exception& e)
        {
            LOG_ERROR(LOG_ROOT()) << "ConfigVar::toString exception "
                << e.what() << " convert: " << TypeToName<T>() << " to string"
                << " name=" << m_name;
        }
        return "";
    }

    // 传入string，将其转成相应的配置值类型，并该当前对象进行赋值
    bool fromString(const std::string& val) override
    {
        try
        {
            setValue(FromStr()(val));
        }
        catch (std::exception& e)
        {
            LOG_ERROR(LOG_ROOT()) << "ConfigVar::fromString exception "
                << e.what() << " convert: string to " << TypeToName<T>()
                << " name=" << m_name
                << " - " << val;
        }
        return false;
    }

    // 获取配置值
    const T getValue()
    {
        RWMutexType::ReadLock lock(m_mutex);
        return m_val;
    }

    // 设置配置值
    void setValue(const T& v)
    {
        {
            RWMutexType::ReadLock lock(m_mutex);
            if(v == m_val) // 相等则直接返回
                return;
            for(auto& i : m_cbs) // 不相等则调用注册的回调函数
                i.second(m_val, v);
        }
        RWMutexType::WriteLock lock(m_mutex);
        m_val = v;
    }

    std::string getTypeName() const override { return TypeToName<T>(); }

    // 为配置类注册回调函数
    uint64_t addListener(on_change_cb cb)
    {
        static uint64_t s_fun_id = 0; // 采用静态变量，保证唯一标识
        RWMutexType::WriteLock lock(m_mutex);
        ++s_fun_id;
        m_cbs[s_fun_id] = cb;
        return s_fun_id;
    }

    // 删除一个注册的回调函数
    void delListener(uint64_t key)
    {
        RWMutexType::WriteLock lock(m_mutex);
        m_cbs.erase(key);
    }

    // 根据标识符返回注册的回调函数
    on_change_cb getListener(uint64_t key)
    {
        RWMutexType::ReadLock lock(m_mutex);
        auto it = m_cbs.find(key);
        return it == m_cbs.end() ? nullptr : it->second;
    }

    // 清空所有的回调函数
    void clearListener()
    {
        RWMutexType::WriteLock lock(m_mutex);
        m_cbs.clear();
    }

private:
    RWMutexType m_mutex;                        // 互斥量
    T m_val;                                    // 配置值
    std::map<uint64_t, on_change_cb> m_cbs;     // 回调函数的容器

}; // class ConfigVar end


// 配置器的管理类
class Config
{
public:
    // 配置器容器
    typedef std::unordered_map<std::string, ConfigVarBase::ptr> ConfigVarMap;

    typedef RWMutex RWMutexType;

    // 传入配置名、配置值、配置描述信息
    // 在容器中进行查找并返回配置类的指针，未找到则创建一个再返回
    template<class T>
    static typename ConfigVar<T>::ptr Lookup(const std::string& name,
                                                const T& default_value,
                                                const std::string& description = "")
    {
        RWMutexType::WriteLock lock(GetMutex());
        auto it = GetDatas().find(name);
        if(it != GetDatas().end())
        {
            auto tmp = std::dynamic_pointer_cast<ConfigVar<T> >(it->second);
            if(tmp)
            {
                LOG_INFO(LOG_ROOT()) << "Lookup name=" << name << " exists";
                return tmp;
            }
            else
            {
                LOG_ERROR(LOG_ROOT()) << "Lookup name=" << name << " exists but type not "
                        << TypeToName<T>() << " real_type=" << it->second->getTypeName()
                        << " " << it->second->toString();
                return nullptr;
            }
        }

        if(name.find_first_not_of("abcdefghikjlmnopqrstuvwxyz._012345678")
                != std::string::npos)
        {
            LOG_ERROR(LOG_ROOT()) << "Lookup name invalid " << name;
            throw std::invalid_argument(name);
        }

        typename ConfigVar<T>::ptr v(new ConfigVar<T>(name, default_value, description));
        GetDatas()[name] = v;
        return v;
    }

    // 传入配置名在容器中进行查找，返回配置类的指针，未找到返回空指针
    template<class T>
    static typename ConfigVar<T>::ptr Lookup(const std::string& name)
    {
        RWMutexType::ReadLock lock(GetMutex());
        auto it = GetDatas().find(name);
        if(it == GetDatas().end())
            return nullptr;
        return std::dynamic_pointer_cast<ConfigVar<T> >(it->second);
    }

    // 从yaml文件中加载配置到当前环境
    static void LoadFromYaml(const YAML::Node& root);

    static void LoadFromConfDir(const std::string& path, bool force = false);

    // 传入配置名在容器中进行查找，返回配置类基类的指针，未找到返回空指针
    static ConfigVarBase::ptr LookupBase(const std::string& name);

    static void Visit(std::function<void(ConfigVarBase::ptr)> cb);

private:
    // 返回配置器的容器
    static ConfigVarMap& GetDatas()
    {
        static ConfigVarMap s_datas; // 保证单例唯一，且在使用前初始化
        return s_datas;
    }

    // 返回互斥量
    static RWMutexType& GetMutex()
    {
        static RWMutexType s_mutex; // 保证单例唯一，且在使用前初始化
        return s_mutex;
    }

}; // class Config end



} // namespace shiosylar end

#endif