#include "health_service.h"
namespace lms::services {
HealthInfo HealthService::check() const {
#ifdef _WIN32
    const std::string platform = "windows";
#elif defined(__linux__)
    const std::string platform = "linux";
#else
    const std::string platform = "other";
#endif
    // 这里只报告进程可响应；数据库就绪检查在接入数据库时单独增加。
    return {"lms-backend", "ok", platform};
}
}
