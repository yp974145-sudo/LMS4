# LMS4 双平台 Drogon 入口

阶段01代码讲解与面试问答  2026年11月5日

作者为项目辅助助手，读者为准备C++实习的初学者。本阶段沿现有LMS4新增成熟框架入口，先证明Windows原生可编译、可响应HTTP。Linux只有构建与测试入口，没有运行证据。本阶段助手实现，用户尚未通过独立练习。

## 目标与修改范围

backend/src/main.cpp组装服务并启动Drogon；controllers/health_controller负责HTTP；services/health_service负责返回健康信息。CMakeLists和CMakePresets统一构建。健康检查不需要SQL，因此没有为它虚构Repository层；后续订单接口再接实际数据访问。

新增backend/tests/check_http.py隔离HTTP测试、Windows与Linux构建脚本、依赖锁清单与恢复脚本、START-BACKEND.cmd和Linux CI。旧src、MySQL业务和原网页入口保留，本轮没有修改订单数据、数据库密码或状态编号。

## 运行与验收

Windows运行backend/build-windows.ps1完成CMake构建与CTest。双击START-BACKEND.cmd，访问http://127.0.0.1:18082/api/health。原业务仍用START-MYSQL.cmd和18080端口。

实测为Windows MinGW GCC15.2、Drogon1.9.14、官方CMake3.31.6：编译链接成功，真实HTTP健康200、平台标记windows、未知路径404、四个非法端口拒绝启动，CTest 1/1通过。测试使用随机端口，结束后终止测试进程。

Linux本机WSL未安装，未实跑。Ubuntu24.04官方libdrogon-dev为1.8.7，与Windows版本不同。Linux脚本和CI均待执行，不把文件存在当成跨平台验证。依赖含MySQL/Redis客户端也不代表业务已接通。

## 从入口到结果

main → registerController → Drogon路由 → HealthController::get → HealthService::check → JsonCpp构造响应 → callback交回框架发送。main先执行；请求函数仅在收到请求后执行。

## 入口代码怎么读

auto controller = std::make_shared<HealthController>();
创建控制器对象。shared_ptr表示共享所有权，注册后框架持有它，不依赖main里的临时引用。

drogon::app().registerController(controller);
把控制器注册到框架。随后addListener设置回环地址与端口，setThreadNum(2)设置I/O线程数，run进入事件循环，直到停止。线程数不是并发能力的测试结果。

## 控制器与业务类

HealthController.h声明类、路由和get函数；.cpp写响应处理。它继承HttpController并关闭自动创建，使用main显式注册。这样对象来源和寿命更容易追踪。

get收到request和callback。request是框架解析好的HTTP请求；callback是可调用对象，参数是响应指针。&&是右值引用，属于框架规定的处理函数签名，不表示这里新建线程。const表示此成员函数不修改控制器成员。

const auto info = service_.check();
调用业务类取得普通C++结构体，然后把service、status、platform放进Json::Value。HTTP状态和JSON格式属于控制器职责；业务结果属于Service。callback交回响应后，底层Socket发送由Drogon负责。

HealthService用编译条件区分_WIN32与__linux__。它只报告进程可响应，不能证明MySQL可用。当前函数短且不访问外部资源，所以可在I/O回调中直接调用；后续同步数据库或重任务不能照搬到事件循环里阻塞。

## 失败与资源释放

main捕获非法端口和启动异常并返回非零；未知路径由框架返回404。测试使用finally终止进程，TemporaryDirectory退出自动清理专属日志目录。依赖故障曾表现为0xc0000139，定位到拆分运行库，补齐隔离libgcc和libstdc++后验证通过，没有覆盖系统DLL。

## 面试问题与继续追问

1 问：为什么选择Drogon？
答：使用成熟HTTP、路由与JSON能力，把精力放在业务与一致性。现阶段只完成框架入口，旧自研HTTP没有冒充Drogon。
追问：是否已支持Linux？
答：源码、构建入口已准备，Linux实际结果尚缺，不能宣称验证通过。

2 问：Controller和Service有什么区别？
答：控制器处理HTTP和响应，业务类返回普通业务结果。对应HealthController::get和HealthService::check。
追问：为什么没有健康仓库类？
答：此接口没有数据库读写，为凑层数增加空仓库没有实际职责。

3 问：callback是什么？
答：框架传入的响应回调，可像函数一样调用，把响应交还给框架。
追问：异步签名是否意味着每次启动线程？
答：不是，回调与线程是不同概念，工作在哪个线程必须看执行路径。

4 问：shared_ptr解决什么？
答：本阶段用它明确注册对象由框架持有，避免临时对象销毁后继续访问。
追问：是否因此线程安全？
答：不是，共享所有权不自动保护对象内部可变数据。

5 问：健康检查证明什么？
答：证明进程可接收并返回一次HTTP请求，不代表数据库、缓存或所有业务正常。
追问：以后怎么检查数据库？
答：单独增加readiness就绪检查，设置超时并控制暴露内容。

## 独立练习与未测项

练习：为响应增加一个表示接口版本的字段，先说明改Controller还是Service，以及为什么；用HTTP测试核对返回值。本练习未代做，完成前不记录为用户已掌握。

未测：Linux运行、数据库连接、权限与订单迁移、长时间稳定性、性能、多实例和部署恢复。依赖恢复脚本尚未在完全干净的Windows环境整套重装验证；CI文件未推送、未远程执行。正式文档与程序在E盘；一次性脚本、下载和渲染位于jk/x本次任务目录，结束清理。