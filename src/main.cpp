#include "lms/api_handlers.h"
#include "lms/config.h"
#include "lms/file_storage.h"
#ifdef LMS_USE_DROGON
#include "lms/drogon_bridge.h"
#endif
#ifdef LMS_HAS_MYSQL
#include "lms/connection_pool.h"
#endif
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <random>
#include <chrono>
#include <filesystem>

extern std::shared_ptr<lms::IUserRepository> make_user_repo_file(const std::string& directory = ".");
extern std::shared_ptr<lms::IOrderRepository> make_order_repo_file(const std::string& directory = ".");
#ifdef LMS_HAS_MYSQL
extern std::shared_ptr<lms::IUserRepository> make_user_repo_mysql();
extern std::shared_ptr<lms::IOrderRepository> make_order_repo_mysql();
#endif
namespace { volatile std::sig_atomic_t stopping = 0; void stop_handler(int) { stopping=1; } }
int main(int argc, char** argv) {
    try {
        std::string config_path = "config/config.json";
        std::optional<int> port_override;
        bool explicit_config = false;
        for (int i = 1; i < argc; i += 2) {
            if (i + 1 >= argc) throw std::invalid_argument("Usage: lms-server [--config PATH] [--port N]");
            const std::string option(argv[i]);
            if (option == "--config" && !explicit_config) { config_path = argv[i+1]; explicit_config = true; }
            else if (option == "--port" && !port_override) port_override = lms::AppConfig::parse_int(argv[i+1], "--port");
            else throw std::invalid_argument("Unknown or repeated option");
        }
        if (explicit_config && !std::ifstream(config_path)) throw std::runtime_error("Cannot open configuration file");
        auto cfg = lms::AppConfig::load(config_path);
        cfg.apply_environment();
        if (port_override) cfg.port = *port_override;
        cfg.validate();
#ifndef LMS_HAS_MYSQL
        if (cfg.mysql.enabled) throw std::runtime_error("MySQL requested but not compiled; use -DLMS_MYSQL=ON or --config config/learning.json");
#endif
        std::string secret;
        if(const char* value=std::getenv("LMS_JWT_SECRET")) secret=value;
        if(secret.empty()) {
            std::random_device random;
            for(int i=0;i<8;++i) secret+=std::to_string(random());
            std::cout << "Temporary signing key: restart invalidates demo tokens.\n";
        }
        if(secret.size()<32) throw std::invalid_argument("LMS_JWT_SECRET must contain at least 32 bytes");
        lms::Logger::instance().set_file(cfg.log.file);
        if (cfg.log.level == "debug") lms::Logger::instance().set_level(lms::Logger::DEBUG);
        std::unique_ptr<lms::file_storage::DirectoryLock> data_lock;
        std::shared_ptr<lms::IUserRepository> user_repo;
        std::shared_ptr<lms::IOrderRepository> order_repo;
#ifdef LMS_HAS_MYSQL
        if (cfg.mysql.enabled) {
            lms::ConnectionPool::instance().init({cfg.mysql.host, cfg.mysql.user, cfg.mysql.password,
                cfg.mysql.database, cfg.mysql.port, cfg.mysql.pool_size});
            user_repo = make_user_repo_mysql();
            order_repo = make_order_repo_mysql();
        } else
#endif
        {
            std::filesystem::create_directories(cfg.data_directory);
            data_lock = std::make_unique<lms::file_storage::DirectoryLock>(cfg.data_directory);
            user_repo = make_user_repo_file(cfg.data_directory);
            order_repo = make_order_repo_file(cfg.data_directory);
        }
        lms::UserService users(user_repo);
        lms::OrderService orders(order_repo);
#ifdef LMS_USE_DROGON
        lms::run_drogon(cfg,users,orders,secret);
        return 0;
#else
        lms::http::Server server;
        server.set_port(cfg.port).set_thread_count(cfg.thread_count).set_bind_address(cfg.bind_address);
        lms::api::register_routes(server.router(),users,orders,secret,cfg.mysql.enabled);
        std::signal(SIGINT,stop_handler); std::signal(SIGTERM,stop_handler);
        if(!server.start()) throw std::runtime_error("Cannot start local listener");
        std::cout << "LMS: http://" << cfg.bind_address << ":" << cfg.port
                  << " backend=" << (cfg.mysql.enabled ? "mysql" : "file") << std::endl;
        while(!stopping) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        server.stop();
        return 0;
#endif
    } catch(const std::exception& error) { std::cerr << error.what() << std::endl; return 1; }
}
