#include <drogon/drogon.h>
#include "controllers/health_controller.h"
#include <iostream>
int main(int argc, char** argv) {
    try {
        int port = 18082;
        if (argc == 3 && std::string(argv[1]) == "--port") {
            const std::string text(argv[2]);
            if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
                throw std::invalid_argument("Invalid port");
            port = std::stoi(text);
        } else if (argc != 1) {
            throw std::invalid_argument("Usage: lms-backend [--port N]");
        }
        if (port < 1 || port > 65535) throw std::invalid_argument("Invalid port");
        auto controller = std::make_shared<lms::controllers::HealthController>();
        drogon::app().registerController(controller);
        drogon::app().addListener("127.0.0.1", port).setThreadNum(2).run();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
