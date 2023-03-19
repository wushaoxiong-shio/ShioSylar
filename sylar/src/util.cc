#include "../include/util.h"
#include "../include/logger.h"
#include "../include/fiber.h"

#include <execinfo.h>
#include <sys/time.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <ifaddrs.h>

namespace shiosylar
{

static shiosylar::Logger::ptr g_logger = LOG_NAME("system");

// 返回当前线程的tid
pid_t GetThreadId()
{
	return syscall(SYS_gettid);
}

// 返回当前的协程ID
uint32_t GetFiberId()
{
	return shiosylar::Fiber::GetFiberId();
}

// 去除函数堆栈后面的偏移量，看起来更整洁
static std::string demangle(const char* str)
{
	size_t size = 0;
	int status = 0;
	std::string rt;
	rt.resize(256);
	if(1 == sscanf(str, "%*[^(]%*[^_]%255[^)+]", &rt[0]))
	{
		char* v = abi::__cxa_demangle(&rt[0], nullptr, &size, &status);
		if(v)
		{
			std::string result(v);
			free(v);
			return result;
		}
	}
	if(1 == sscanf(str, "%255s", &rt[0]))
		return rt;
	return str;
}

// 获取当前的调用栈 bt 保存调用栈 size 最多返回层数 skip 跳过栈顶的层数
void Backtrace(std::vector<std::string>& bt, int size, int skip)
{
	void** array = (void**)malloc((sizeof(void*) * size));
	size_t s = ::backtrace(array, size);

	char** strings = backtrace_symbols(array, s);
	if(strings == NULL)
	{
		LOG_ERROR(g_logger) << "backtrace_synbols error";
		return;
	}

	for(size_t i = skip; i < s; ++i)
		bt.push_back(demangle(strings[i]));

	free(strings);
	free(array);
}

// 获取当前栈信息的字符串size 栈的最大层数 skip 跳过栈顶的层数 prefix 栈信息前输出的内容
std::string BacktraceToString(int size, int skip, const std::string& prefix)
{
	std::vector<std::string> bt;
	Backtrace(bt, size, skip);
	std::stringstream ss;
	for(size_t i = 0; i < bt.size(); ++i)
		ss << prefix << bt[i] << std::endl;
	return ss.str();
}

uint64_t GetCurrentMS()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000ul  + tv.tv_usec / 1000;
}

uint64_t GetCurrentUS()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000 * 1000ul  + tv.tv_usec;
}


// lstat 查看文件属性，成功时返回0
static int __lstat(const char* file, struct stat* st = nullptr)
{
	struct stat lst;
	int ret = lstat(file, &lst);
	if(st)
		*st = lst;
	return ret;
}

// mkdir 创建目录，成功时返回0
static int __mkdir(const char* dirname)
{
	if(access(dirname, F_OK) == 0)
		return 0;
	return mkdir(dirname, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
}

// 创建目录，一级一级向下创建目录
bool FSUtil::Mkdir(const std::string& dirname)
{
	if(__lstat(dirname.c_str()) == 0)
		return true;
	// strdup 复制字符串，在堆上开辟，需要手动释放
	char* path = strdup(dirname.c_str());
	char* ptr = strchr(path + 1, '/');
	do
	{
		for(; ptr; *ptr = '/', ptr = strchr(ptr + 1, '/'))
		{
			*ptr = '\0';
			if(__mkdir(path) != 0)
				break;
		}
		if(ptr != nullptr)
			break;
		else if(__mkdir(path) != 0)
			break;
		free(path);
		return true;
	}
	while(0);
	free(path);
	return false;
}

// 返回文件所在的目录
std::string FSUtil::Dirname(const std::string& filename)
{
	if(filename.empty())
		return ".";
	auto pos = filename.rfind('/');
	if(pos == 0)
		return "/";
	else if(pos == std::string::npos)
		return ".";
	else
		return filename.substr(0, pos);
}

bool FSUtil::OpenForWrite(std::ofstream& ofs,
							const std::string& filename,
							std::ios_base::openmode mode)
{
	ofs.open(filename.c_str(), mode);
	if(!ofs.is_open())
	{
		std::string dir = Dirname(filename);
		Mkdir(dir);
		ofs.open(filename.c_str(), mode);
	}
	return ofs.is_open();
}


} //namespace shiosylar end