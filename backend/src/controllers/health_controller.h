#pragma once
#include <drogon/HttpController.h>
#include "services/health_service.h"
namespace lms::controllers {
// 显式注册并由框架持有，避免请求回调引用临时对象。
class HealthController : public drogon::HttpController<HealthController, false> {
    services::HealthService service_;
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(HealthController::get, "/api/health", drogon::Get);
    METHOD_LIST_END
    void get(const drogon::HttpRequestPtr& request,
             std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;
};
}
