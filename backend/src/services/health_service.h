#pragma once
#include <string>
namespace lms::services {
struct HealthInfo {
    std::string service;
    std::string status;
    std::string platform;
};
class HealthService {
public:
    HealthInfo check() const;
};
}
