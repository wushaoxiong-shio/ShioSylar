#ifndef __SHIOSYLAR_UTIL_H__
#define __SHIOSYLAR_UTIL_H__

// 系统工具

// #include "sylar/util/hash_util.h"
// #include "sylar/util/json_util.h"
// #include "sylar/util/crypto_util.h"

#include <cxxabi.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <stdint.h>
#include <vector>
#include <string>
#include <iomanip>
// #include <json/json.h>
#include <yaml-cpp/yaml.h>
#include <fstream>
#include <iostream>
#include <boost/lexical_cast.hpp>
// #include <google/protobuf/message.h>


namespace shiosylar
{

pid_t GetThreadId(); // 返回当前线程的tid

uint32_t GetFiberId(); // 返回当前的协程ID

// 获取当前的调用栈 bt 保存调用栈 size 最多返回层数 skip 跳过栈顶的层数
void Backtrace(std::vector<std::string>& bt, int size = 64, int skip = 1);

// 获取当前栈信息的字符串size 栈的最大层数 skip 跳过栈顶的层数 prefix 栈信息前输出的内容
std::string BacktraceToString(int size = 64, int skip = 2, const std::string& prefix = "");

uint64_t GetCurrentMS();

uint64_t GetCurrentUS();


template<class T>
const char* TypeToName()
{
	static const char* s_name = abi::__cxa_demangle(typeid(T).name(), nullptr, nullptr, nullptr);
	return s_name;
}

// 文件操作类
class FSUtil
{
public:
	static bool Mkdir(const std::string& dirname);

	static std::string Dirname(const std::string& filename);

	static bool OpenForWrite(std::ofstream& ofs,
								const std::string& filename,
								std::ios_base::openmode mode);
};

} // namespace shiosylar end



#endif