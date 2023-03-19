#ifndef __SHIOSYLAR_NONCOPYABLE_H__
#define __SHIOSYLAR_NONCOPYABLE_H__

// 不可拷贝类

namespace shiosylar
{

class noncopyable
{
public:
    noncopyable() = default;

    ~noncopyable() = default;

    noncopyable(const noncopyable&) = delete;

    noncopyable& operator=(const noncopyable&) = delete;

}; // class noncopyable end

} // namespace shiosylar end

#endif
