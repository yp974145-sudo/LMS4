#include "service.hpp"
#include "lms/http_server.h"
#include "lms/json.h"
#include <csignal>
#include <iostream>
#include <limits>
namespace { volatile std::sig_atomic_t stopping=0;void stop(int){stopping=1;} }
int main(int argc,char** argv) {
    if(argc!=2){std::cerr<<"Usage: 03_http PORT\n";return 2;}
    auto repo=std::make_shared<lesson::MemoryOrderRepository>();
    lesson::OrderService orders(repo);
    lms::http::Server server;
    server.set_port(std::stoi(argv[1])).set_thread_count(2).set_bind_address("127.0.0.1");
    server.router().get("/api/health",[](const auto&,auto& response){
        response.set_json("{\"message\":\"lesson ready\"}");
    });
    server.router().post("/api/orders",[&orders](const auto& request,auto& response){
        try {
            auto data=lms::Json::parse(request.body);
            if(!data["quantity"].is_int()) throw std::invalid_argument("integer required");
            auto quantity=data["quantity"].as_int();
            if(quantity<1 || quantity>std::numeric_limits<int>::max())
                throw std::invalid_argument("quantity out of range");
            lesson::Order input;
            input.customer=data["customer"].as_string();input.cargo=data["cargo"].as_string();
            input.quantity=static_cast<int>(quantity);
            // Stage-local actor only. Authentication is deliberately added later.
            // This isolated prototype must not be deployed as an authenticated app.
            const lesson::User actor{"lesson-customer",lesson::Role::Customer};
            auto result=orders.create(input,actor);
            if(!result.ok) throw std::invalid_argument(result.error);
            response.status=201;
            response.set_json(lms::Json::make_object({{"id",result.order.id},
                {"creator",result.order.creator},{"quantity",result.order.quantity}}).dump());
        } catch(const std::exception&) {
            response.status=400;response.set_json("{\"error\":\"invalid lesson order\"}");
        }
    });
    std::signal(SIGINT,stop);std::signal(SIGTERM,stop);
    if(!server.start()) return 3;
    std::cout<<"Lesson server ready\n"<<std::flush;
    while(!stopping)std::this_thread::sleep_for(std::chrono::milliseconds(50));
    server.stop();
}
