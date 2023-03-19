#include "logger.h"
#include "config.h"
#include "thread.h"
#include "macro.h"
#include "fiber.h"
#include "scheduler.h"

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


int main(int argc, char* argv[])
{
    LOG_DEBUG(g_logger) << "test";
    return 0;
}
