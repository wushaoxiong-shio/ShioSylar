#ifndef __SHIOSYLAR_SINGLETON_H__
#define __SHIOSYLAR_SINGLETON_H__

// 单例类

#include <memory>

namespace shiosylar
{

// 单例对象类，返回指针
template<class T, class X = void, int N = 0>
class Singleton
{
public:
    static T* GetInstance()
    {
        static T v;
        return &v;
        //return &GetInstanceX<T, X, N>();
    }
};

// 单例智能指针类，返回智能指针对象
template<class T, class X = void, int N = 0>
class SingletonPtr
{
public:
    static std::shared_ptr<T> GetInstance()
    {
        static std::shared_ptr<T> v(new T);
        return v;
        //return GetInstancePtr<T, X, N>();
    }
};





} // namespace shiosylar end

#endif