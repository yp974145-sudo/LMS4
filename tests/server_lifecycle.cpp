#include "lms/http_server.h"
#include <chrono>
#include <iostream>
#include <thread>
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    for (int i=0; i<3; ++i) {
        lms::http::Server server;
        server.set_port(std::stoi(argv[1])).set_thread_count(2);
        server.router().get("/",[](const auto&,auto& res){res.set_json("{}");});
        if(!server.start()) return 3;
        std::cout << "READY" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        server.stop();
        server.stop(); // Idempotent shutdown, then reuse the port in the next scope.
        std::cout << "STOPPED" << std::endl;
    }
}
