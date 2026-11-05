#include "health_controller.h"
namespace lms::controllers {
void HealthController::get(const drogon::HttpRequestPtr&,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) const {
    const auto info = service_.check();
    Json::Value json;
    json["service"] = info.service;
    json["status"] = info.status;
    json["platform"] = info.platform;
    json["framework"] = "drogon";
    callback(drogon::HttpResponse::newHttpJsonResponse(json));
}
}
