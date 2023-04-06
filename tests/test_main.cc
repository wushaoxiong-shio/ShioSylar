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
