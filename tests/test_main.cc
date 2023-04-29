#include "config.h"
#include "fiber.h"
#include "logger.h"
#include "macro.h"
#include "scheduler.h"
#include "thread.h"

class loged
{
public:
    loged()
    {
        YAML::Node config = YAML::LoadFile("../Inl/log.yaml");
        shiosylar::Config::LoadFromYaml(config);
    }
};
loged logs;

shiosylar::Logger::ptr g_logger = LOG_NAME("shio");

int main(int argc, char *argv[])
{
    int a = 1;
    LOG_INFO(g_logger) << "shio test";
    LOG_FMT_INFO(g_logger, "%d--%s", a, "sl");
    return 0;
}