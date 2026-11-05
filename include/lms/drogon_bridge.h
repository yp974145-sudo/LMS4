#pragma once
#include <drogon/drogon.h>
#include <trantor/utils/ConcurrentTaskQueue.h>
#include "lms/api_handlers.h"
#include "lms/config.h"

namespace lms {
// 过渡适配：Drogon负责网络，复用既有业务接口与权限。
// 同步仓库调用放到框架工作队列，不能阻塞Drogon的I/O线程。
inline void run_drogon(const AppConfig& config, UserService& users,
                       OrderService& orders, const std::string& secret) {
    http::Router router;
    api::register_routes(router, users, orders, secret, config.mysql.enabled);
    trantor::ConcurrentTaskQueue workers(config.thread_count, "lms-business");
    std::atomic<int> pending{0};
    drogon::app().registerPreRoutingAdvice(
        [&](const drogon::HttpRequestPtr& request, drogon::AdviceCallback&& respond,
            drogon::AdviceChainCallback&&) {
            if (pending.fetch_add(1) >= 256) {
                --pending;
                auto response=drogon::HttpResponse::newHttpResponse();
                response->setStatusCode(drogon::k503ServiceUnavailable);
                response->setContentTypeCode(drogon::CT_APPLICATION_JSON);
                response->setBody("{\"error\":\"Service busy\"}");
                respond(response);return;
            }
            workers.runTaskInQueue([&,request,respond=std::move(respond)] {
                struct CountGuard {std::atomic<int>& count;~CountGuard(){--count;}} guard{pending};
                http::Response result;
                try {
                    http::Request input;
                    input.method=request->methodString();input.path=request->path();
                    input.body=std::string(request->body());
                    for(const auto& pair:request->headers()) input.headers.insert(pair);
                    for(const auto& pair:request->getParameters()) input.query_params.insert(pair);
                    router.handle(input,result);
                } catch(const RepositoryError&) {
                    result.status=500;result.set_json("{\"error\":\"Data storage unavailable\",\"code\":500}");
                } catch(const std::invalid_argument&) {
                    result.status=400;result.set_json("{\"error\":\"Invalid request parameter\"}");
                } catch(const std::out_of_range&) {
                    result.status=400;result.set_json("{\"error\":\"Request parameter out of range\"}");
                } catch(const std::exception&) {
                    result.status=500;result.set_json("{\"error\":\"Internal server error\"}");
                }
                auto response=drogon::HttpResponse::newHttpResponse();
                response->setStatusCode(static_cast<drogon::HttpStatusCode>(result.status));
                for(const auto& pair:result.headers) response->addHeader(pair.first,pair.second);
                response->setBody(result.body);
                respond(response);
            });
        });
    drogon::app().addListener(config.bind_address,config.port)
        .setClientMaxBodySize(1024*1024).setThreadNum(2).run();
    workers.stop();
}
}
