#pragma once

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <unistd.h>
    #include <arpa/inet.h>
#endif

#include "lms/file_storage.h"
#include "lms/logger.h"
#include <string>
#include <map>
#include <functional>
#include <thread>
#include <vector>
#include <sstream>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <mutex>
#include <atomic>
#include <set>
#include <stdexcept>
#include <cctype>

namespace lms::http {

// ========== 请求 ==========
struct Request {
    std::string method;
    std::string path;
    std::map<std::string, std::string> headers;
    std::string body;

    // URL 查询参数 ?key=val&...
    std::map<std::string, std::string> query_params;

    // 路径参数 /api/orders/:id
    std::map<std::string, std::string> path_params;

    std::string get_header(const std::string& key, const std::string& def = "") const {
        auto it = headers.find(key);
        // 不区分大小写查找
        for (auto& [k, v] : headers) {
            std::string kl = k;
            std::transform(kl.begin(), kl.end(), kl.begin(), ::tolower);
            std::string keyl = key;
            std::transform(keyl.begin(), keyl.end(), keyl.begin(), ::tolower);
            if (kl == keyl) return v;
        }
        return def;
    }
};

// ========== 响应 ==========
struct Response {
    int status = 200;
    std::map<std::string, std::string> headers;
    std::string body;

    Response& set_status(int s) { status = s; return *this; }
    Response& set_body(const std::string& b) { body = b; return *this; }
    Response& set_json(const std::string& json_str) {
        headers["Content-Type"] = "application/json; charset=utf-8";
        body = json_str;
        return *this;
    }
    Response& set_html(const std::string& html_str) {
        headers["Content-Type"] = "text/html; charset=utf-8";
        body = html_str;
        return *this;
    }
    Response& set_cors() {
        headers["Access-Control-Allow-Origin"] = "*";
        headers["Access-Control-Allow-Methods"] = "GET,POST,PUT,DELETE,OPTIONS";
        headers["Access-Control-Allow-Headers"] = "Content-Type,Authorization";
        return *this;
    }
};

using Handler = std::function<void(const Request&, Response&)>;

// ========== 路由器 ==========
class Router {
public:
    void get(const std::string& pattern, Handler h)  { routes_.push_back({"GET", pattern, std::move(h)}); }
    void post(const std::string& pattern, Handler h) { routes_.push_back({"POST", pattern, std::move(h)}); }
    void put(const std::string& pattern, Handler h)  { routes_.push_back({"PUT", pattern, std::move(h)}); }
    void del(const std::string& pattern, Handler h)  { routes_.push_back({"DELETE", pattern, std::move(h)}); }

    // 快速注册静态文件路由
    void serve_static(const std::string& url_path, const std::string& file_path, const std::string& mime = "text/html") {
        get(url_path, [file_path, mime](const Request&, Response& res) {
            std::ifstream f(file_path, std::ios::binary);
            if (!f) { res.status = 404; res.set_body("File not found"); return; }
            std::stringstream ss;
            ss << f.rdbuf();
            res.headers["Content-Type"] = mime + "; charset=utf-8";
            res.body = ss.str();
        });
    }

    void handle(const Request& req, Response& res) {
        // 预检请求
        if (req.method == "OPTIONS") {
            res.status = 204;
            return;
        }

        // 找到匹配的 handler（优先完全匹配）
        Handler* best = nullptr;
        size_t best_params = 0;

        for (auto& r : routes_) {
            if (r.method != req.method) continue;
            if (r.pattern == req.path) { best = &r.handler; best_params = SIZE_MAX; break; }
            // 路径参数匹配
            auto params = match_path_params(r.pattern, req.path);
            if (!params.empty() && params.size() > best_params) {
                // 有匹配但不如完全匹配好
                best_params = params.size();
            }
        }

        if (best) {
            (*best)(req, res);
        } else {
            // 尝试路径参数匹配
            for (auto& r : routes_) {
                if (r.method != req.method) continue;
                auto params = match_path_params(r.pattern, req.path);
                if (!params.empty()) {
                    Request mutable_req = req;
                    mutable_req.path_params = params;
                    r.handler(mutable_req, res);
                    return;
                }
            }
            // 没匹配到
            res.status = 404;
            res.set_json(R"({"error":"Not Found","path":")" + req.path + "\"}");
        }
    }

private:
    struct Route {
        std::string method;
        std::string pattern;
        Handler handler;
    };
    std::vector<Route> routes_;

    static std::map<std::string, std::string> match_path_params(
        const std::string& pattern, const std::string& path)
    {
        std::map<std::string, std::string> params;
        size_t pi = 0, pa = 0;
        std::string cur_name, cur_val;

        while (pi < pattern.size() && pa < path.size()) {
            if (pattern[pi] == ':') {
                ++pi; // skip ':'
                cur_name.clear();
                while (pi < pattern.size() && pattern[pi] != '/') cur_name += pattern[pi++];
                cur_val.clear();
                while (pa < path.size() && path[pa] != '/') cur_val += path[pa++];
                params[cur_name] = cur_val;
            } else {
                if (pattern[pi] != path[pa]) return {}; // 不匹配
                ++pi; ++pa;
            }
        }
        if (pi == pattern.size() && pa == path.size()) return params;
        return {};
    }
};

// ========== HTTP 服务器 ==========
// Fixed worker count + bounded blocking I/O. This is not an epoll server.
class Server {
#ifdef _WIN32
    using Socket = SOCKET;
    static constexpr Socket invalid = INVALID_SOCKET;
#else
    using Socket = int;
    static constexpr Socket invalid = -1;
#endif
    struct BadRequest : std::runtime_error { using std::runtime_error::runtime_error; };
    std::atomic<bool> running_{false};
    Socket sock_ = invalid;
    int port_ = 8080, thread_count_ = 4;
    std::string bind_address_ = "127.0.0.1";
    Router router_;
    std::vector<std::thread> threads_;
    std::mutex clients_mutex_;
    std::set<Socket> clients_;
    static constexpr size_t max_headers = 16384, max_body = 1024 * 1024;
    static void close_socket(Socket fd) {
#ifdef _WIN32
        closesocket(fd);
#else
        ::close(fd);
#endif
    }
    static void interrupt(Socket fd) {
#ifdef _WIN32
        ::shutdown(fd, SD_BOTH);
#else
        ::shutdown(fd, SHUT_RDWR);
#endif
    }
    static std::string lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
        return s;
    }
    static std::string decode_url(const std::string& value) {
        std::string out;
        for (size_t i = 0; i < value.size(); ++i) {
            if (value[i] == '%') {
                if (i + 2 >= value.size() || !std::isxdigit(static_cast<unsigned char>(value[i+1])) ||
                        !std::isxdigit(static_cast<unsigned char>(value[i+2]))) throw BadRequest("Invalid URL escape");
                out += static_cast<char>(std::stoi(value.substr(i+1,2), nullptr, 16)); i += 2;
            } else out += value[i] == '+' ? ' ' : value[i];
        }
        return out;
    }
    static bool receive(Socket fd, std::string& buffer) {
        char bytes[8192];
        int n = static_cast<int>(::recv(fd, bytes, sizeof(bytes), 0));
        if (n <= 0) return false;
        buffer.append(bytes, static_cast<size_t>(n));
        return true;
    }
    // buffer survives requests: extra bytes belong to the next HTTP request.
    static bool read_request(Socket fd, std::string& buffer, Request& req) {
        size_t end;
        while ((end = buffer.find("\r\n\r\n")) == std::string::npos) {
            if (buffer.size() > max_headers) throw BadRequest("Headers too large");
            if (!receive(fd, buffer)) return false;
        }
        if (end > max_headers) throw BadRequest("Headers too large");
        std::istringstream input(buffer.substr(0, end));
        std::string line, version, extra;
        if (!std::getline(input, line)) throw BadRequest("Missing request line");
        std::istringstream first(line);
        if (!(first >> req.method >> req.path >> version) || (first >> extra) ||
                (version != "HTTP/1.1" && version != "HTTP/1.0") || req.path.empty() || req.path[0] != '/')
            throw BadRequest("Invalid request line");
        while (std::getline(input, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            auto colon = line.find(':');
            if (colon == std::string::npos || colon == 0) throw BadRequest("Invalid header");
            auto name = lower(line.substr(0,colon));
            if (!std::all_of(name.begin(),name.end(),[](unsigned char c){return std::isalnum(c) || c == '-';}))
                throw BadRequest("Invalid header name");
            auto value = line.substr(colon+1);
            auto begin = value.find_first_not_of(" \t");
            value = begin == std::string::npos ? "" : value.substr(begin);
            auto last = value.find_last_not_of(" \t");
            if (last != std::string::npos) value.resize(last+1);
            if (!req.headers.emplace(name,value).second) throw BadRequest("Duplicate headers unsupported");
        }
        if (req.headers.count("transfer-encoding")) throw BadRequest("Transfer encoding unsupported");
        size_t size = 0;
        if (req.headers.count("content-length")) {
            auto value = req.headers.at("content-length");
            if (value.empty() || value.size() > 10 || !std::all_of(value.begin(),value.end(),
                    [](unsigned char c){return c >= '0' && c <= '9';})) throw BadRequest("Invalid Content-Length");
            size = static_cast<size_t>(std::stoull(value));
            if (size > max_body) throw BadRequest("Body too large");
        }
        const size_t total = end + 4 + size;
        while (buffer.size() < total) if (!receive(fd,buffer)) return false;
        req.body.assign(buffer, end+4, size);
        buffer.erase(0,total);
        const auto query = req.path.find('?');
        if (query != std::string::npos) {
            auto values = req.path.substr(query+1); req.path.resize(query);
            std::istringstream pairs(values);
            while (std::getline(pairs,line,'&')) {
                auto equals = line.find('=');
                req.query_params[decode_url(line.substr(0,equals))] =
                    equals == std::string::npos ? "" : decode_url(line.substr(equals+1));
            }
        }
        // Preserve HTTP/1.0 default-close; the worker uses this internal field.
        req.headers["x-lms-http-version"] = version;
        return true;
    }
    static bool write_response(Socket fd, const Response& res, bool keep_alive) {
        std::ostringstream stream;
        stream << "HTTP/1.1 " << res.status << " Response\r\n";
        for (auto& item : res.headers) stream << item.first << ": " << item.second << "\r\n";
        stream << "Content-Length: " << res.body.size() << "\r\nConnection: "
               << (keep_alive ? "keep-alive" : "close") << "\r\n\r\n" << res.body;
        const auto bytes = stream.str();
        size_t sent = 0;
        while (sent < bytes.size()) {
#ifdef _WIN32
            int n = ::send(fd, bytes.data()+sent, static_cast<int>(bytes.size()-sent), 0);
#else
            int n = static_cast<int>(::send(fd, bytes.data()+sent, bytes.size()-sent, MSG_NOSIGNAL));
#endif
            if (n <= 0) return false;
            sent += static_cast<size_t>(n);
        }
        return true;
    }
    void worker(Socket listener) {
        while (running_) {
            Socket fd = ::accept(listener, nullptr, nullptr);
            if (fd == invalid) { if (!running_) return; continue; }
            {
                std::lock_guard<std::mutex> guard(clients_mutex_);
                if (!running_) { close_socket(fd); return; }
                clients_.insert(fd);
            }
#ifdef _WIN32
            DWORD timeout = 2000;
            setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));
            setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));
#else
            timeval timeout{2,0};
            setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
            setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
#endif
            std::string buffer;
            for (int requests = 0; running_ && requests < 100; ++requests) {
                Request req; Response res; bool keep = false;
                try {
                    if (!read_request(fd,buffer,req)) break;
                    const auto connection = lower(req.get_header("Connection"));
                    keep = requests < 99 && connection != "close" &&
                        (req.get_header("x-lms-http-version") == "HTTP/1.1" || connection == "keep-alive");
                    router_.handle(req,res);
                } catch (const RepositoryError& error) {
                    LOG_ERROR(std::string("Repository request failed: ") + error.what());
                    res.status = 500; res.set_json("{\"error\":\"Data storage unavailable; operation was not reported successful\",\"code\":500}"); keep = false;
                } catch (const std::invalid_argument&) {
                    res.status = 400; res.set_json("{\"error\":\"Invalid request parameter\"}"); keep = false;
                } catch (const std::out_of_range&) {
                    res.status = 400; res.set_json("{\"error\":\"Request parameter out of range\"}"); keep = false;
                } catch (const BadRequest&) {
                    res.status = 400; res.set_json("{\"error\":\"Malformed HTTP request\"}"); keep = false;
                } catch (const std::exception&) {
                    res.status = 500; res.set_json("{\"error\":\"Internal server error\"}"); keep = false;
                }
                if (!write_response(fd,res,keep) || !keep) break;
            }
            { std::lock_guard<std::mutex> guard(clients_mutex_); clients_.erase(fd); close_socket(fd); }
        }
    }
public:
    ~Server() { stop(); }
    Server& set_port(int p) { port_=p; return *this; }
    Server& set_thread_count(int n) { thread_count_=n; return *this; }
    Server& set_bind_address(const std::string& address) {
        if (address != "127.0.0.1" && address != "0.0.0.0")
            throw std::invalid_argument("Unsupported bind address");
        bind_address_ = address;
        return *this;
    }
    Router& router() { return router_; }
    bool start() {
        if (running_ || port_ < 1 || port_ > 65535 || thread_count_ < 1 || thread_count_ > 128) return false;
#ifdef _WIN32
        WSADATA data;
        if (WSAStartup(MAKEWORD(2,2),&data)) return false;
#endif
        sock_ = ::socket(AF_INET,SOCK_STREAM,0);
        if (sock_ == invalid) {
#ifdef _WIN32
            WSACleanup();
#endif
            return false;
        }
#ifndef _WIN32
        // Linux retains accepted connections in TIME_WAIT after active close.
        // Reuse the listening address on restart; this does not enable SO_REUSEPORT.
        int reuse = 1;
        if (::setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0) {
            close_socket(sock_); sock_=invalid;
            return false;
        }
#endif
        sockaddr_in addr{}; addr.sin_family=AF_INET; addr.sin_port=htons(static_cast<uint16_t>(port_));
        addr.sin_addr.s_addr=htonl(bind_address_ == "0.0.0.0" ? INADDR_ANY : INADDR_LOOPBACK);
        if (::bind(sock_,reinterpret_cast<sockaddr*>(&addr),sizeof(addr)) != 0 || ::listen(sock_,SOMAXCONN) != 0) {
            close_socket(sock_); sock_=invalid;
#ifdef _WIN32
            WSACleanup();
#endif
            return false;
        }
        running_=true;
        try { for(int i=0;i<thread_count_;++i) threads_.emplace_back(&Server::worker,this,sock_); }
        catch (...) { stop(); throw; }
        return true;
    }
    void stop() {
        if (!running_.exchange(false)) return;
        interrupt(sock_); close_socket(sock_); sock_=invalid;
        { std::lock_guard<std::mutex> guard(clients_mutex_); for (auto fd : clients_) interrupt(fd); }
        for (auto& t : threads_) if(t.joinable()) t.join();
        threads_.clear();
#ifdef _WIN32
        WSACleanup();
#endif
    }
};

} // namespace lms::http
