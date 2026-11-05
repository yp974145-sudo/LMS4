# LMS4：C++ 后台求职学习项目

## 公开仓库与云端验证

公开仓库：https://github.com/yp974145-sudo/LMS4 。代码提交 `cb899c2` 已通过 [基础CI](https://github.com/yp974145-sudo/LMS4/actions/runs/41985628213) 和 [Drogon Linux检查](https://github.com/yp974145-sudo/LMS4/actions/runs/41985628132)。下文较早阶段的“Linux未测”和“尚未上传”是当时记录，当前以本节及验证报告为准。

Ubuntu24.04源码运行入口（文件存储模式）如下；仍需安装 backend/README.md 列出的Drogon开发依赖：

```bash
cmake -S . -B build/app -G Ninja -DLMS_MYSQL=OFF -DLMS_USE_DROGON=ON -DBUILD_HTTP_TESTS=OFF
cmake --build build/app --target lms-server --parallel 2
./build/app/lms-server --config config/learning.example.json
```

在项目根目录运行后，浏览器访问 http://127.0.0.1:18080/ 。仓库没有本机MySQL配置和数据库。此处为本地运行，不是公网在线部署；未验证Linux下本项目MySQL业务、多实例、TLS和长期稳定性。详细结果见 `docs/公开发布与Linux验证.md`。


当前迁移以原 `src/main.cpp` 和根 CMake 为准：Drogon已接入原业务候选，文件模式15项业务/原网页检查通过；原MySQL日常程序尚未替换。`backend/` 的健康接口仅作框架验证材料，不代替原业务。没有删除旧源码或数据，没有上传GitHub。



工作目录：`E:\hanako\LMS4`。源码、备份、程序、学习数据库和正式资料保留在 E 盘。一次性脚本、下载、截图和渲染验证临时文件按最新约定放 C:\Users\23974\Desktop\jk\x 的任务专属子目录并清理。更新日期：2026-11-05。



这是物流公司的内部员工后台：客服建单 → 管理员审核 → 仓储员出库 → 调度员发车、确认送达 → 客服确认完成。已具备文件和真实 MySQL 两种运行方式；分布式升级仍是后续计划。



## 按实际开发顺序学习（2026-10-23）

主教材：`docs/LMS4开发实录-从空工程到现有版本.md`。从需求和空工程开始，逐步完成模型、业务/仓库、HTTP、文件、认证、MySQL、并发、前端、测试与交付，并穿插面经和算法。它是连续开发教程，不是目录或路线表。

网页阅读版：`http://127.0.0.1:18081/`。本地阅读服务关闭后，可在项目根运行 `python -B docs/serve_development_manual.py 18081`；业务网页仍是18080。Markdown及HTML正文都在E盘。

配套初版在 `learning/from-scratch`，运行 `powershell -ExecutionPolicy Bypass -File learning/from-scratch/build.ps1`，再运行 `python -B learning/from-scratch/check_http.py`。使用隔离内存数据和随机测试端口，不覆盖正常业务。

## 现在从这里开始



1. 双击 `START-MYSQL.cmd`，打开 `http://127.0.0.1:18080`。如果服务已经运行，直接打开网页；顶部应显示“MySQL 数据库”。

2. 使用下面账号登录。每个身份的标题、默认页面、菜单和操作按钮不同。

3. 今天只跑“客服建单 → 管理员审核”，跟随 `docs/今天先跑一张订单.md` 找到对应代码。



| 账号 | 演示密码 | 默认页面与操作 |

|---|---|---|

| customer | customer123 | 客服工作台；建单、修改待审核订单、确认完成 |

| admin | admin123 | 管理员工作台；默认待审核，可管理用户、审核及执行员工操作 |

| storage | storage123 | 仓储工作台；默认待出库，可出库、查询 |

| dispatch | dispatch123 | 调度工作台；默认运输任务，可发车、确认送达、查询 |



所有有效员工都可查询订单。隐藏按钮用于界面体验，后端仍独立验证权限；这不是“客户只能查看本人订单”的隔离模型。服务重启后临时签名密钥会变化，旧登录需要重新登录。



## 两种启动方式与数据



| 入口 | 存储位置 | 说明 |

|---|---|---|

| `START-MYSQL.cmd` | `.mysql-learning/data/`，独立实例端口 13306，学习库 `lms_learning` | 当前主要演示方式 |

| `START.cmd` | `data/learning/` | 文件存储学习方式，保留损坏检测与恢复工具 |



两个入口共用网页端口 18080，切换前用 `STOP.cmd` 停止当前业务程序。`STOP-MYSQL.cmd` 另关闭项目自己的数据库实例；原 MySQL 实例不受影响。两种模式各有数据，本轮未自动迁移原订单。



原 `config/config.json` 和根目录 `user.dat` 保留。直接运行程序默认读取原配置；学习时请使用上述明确入口。独立数据库的日志在 `.mysql-learning/logs/`，临时目录在 `.tmp/mysql/`，凭据仅存在本地生成配置中，已加入忽略规则。



## 已验证与尚未验证



以下完整测试记录来自 2026-10-21；2026-10-22 恢复服务并核对后端和数据库路径，没有重新执行全套测试。



- 文件版：17 项 HTTP、8 项启动配置、15 项存储安全测试，共 40 项通过，另有生命周期测试。

- MySQL 能力版：编译通过；文件模式回归 39 项通过，1 项仅文件版适用测试跳过，生命周期通过。

- 真实 MySQL：登录、用户管理、完整订单流转、重启保留、字段边界和 UTF-8、两业务进程竞争审核仅一次成功，均已验证。

- 原生数据库测试：参数绑定及移动、长字段/NULL、连接池争用/唤醒/超时、事务异常回滚与提交，已验证。

- 真实 Edge：文件和 MySQL 后端各 8 组角色界面、刷新恢复及按钮流程检查通过。

- CMake、Docker、Linux 未实际运行；没有吞吐、P95/P99、数据库崩溃恢复或整套高可用结论。



详细证据见 `docs/角色工作台与MySQL-验证记录.md`；文件恢复规则见 `docs/文件存储安全-验证记录.md`。之前的阶段记录保留为历史，不应把其中旧状态当作当前状态。



## 开发与复测



先停止本项目业务程序，再运行 `build.bat`（文件版）或 `powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1 -WithMySQL`（数据库能力版）。`TEST.cmd` 复测文件版；数据库和浏览器测试命令见本轮验证记录。测试使用隔离目录或临时库，不使用真实学习订单。



已有程序运行时，可用 `build.ps1 -OutputName lms-server-roles-candidate` 生成另名候选；SQL 版本加 `-WithMySQL`。不要直接覆盖运行中的 exe。



## 学习与继续开发



先读 `docs/今天先跑一张订单.md`，再读 `docs/第四课-角色界面与MySQL.md`。前三课仍保留，按需阅读，不必一次看完。



下一技术小阶段是待审核订单的并发字段修改冲突；之后完善成熟密码哈希、旧令牌吊销和数据库故障测试。当前仍使用 SHA-256 密码；状态条件更新不等于完整的字段版本控制。Redis、RabbitMQ、gRPC 和多实例可用性尚未实现。



代码整合由助手完成。你独立完成练习、能修改、复测并解释后，再把相应内容记录为个人掌握。项目顺序与六阶段路线见 `docs/学习与升级路线.md`；新聊天先读 `docs/继续开发交接.md`，不要再次整目录覆盖或重新合并 C 盘旧副本。



备份保存在 `migration-backups/`。活跃 MySQL 目录不能随手复制后称作有效备份，数据库需逻辑导出或正常停止独立实例后完整备份。



## 2026年11月5日 框架迁移阶段1

新增 `backend/` 作为现有业务的 Drogon 网络层迁移入口，Windows 原生构建与真实 HTTP 检查通过。当前仅健康接口，不包含登录、订单或数据库迁移。运行 `START-BACKEND.cmd` 后访问 http://127.0.0.1:18082/api/health 。原18080业务入口保留。

Linux脚本与Ubuntu CI已编写，本机WSL未安装，Linux运行未实测。详细构建与边界见 `backend/README.md`。阶段讲解与5组面试问答见 `docs/阶段01_双平台Drogon入口_代码讲解与面试问答.docx`。


## 从 GitHub 获取后的运行边界

本仓库不包含本机数据库目录、账号密码配置、依赖二进制或构建产物。先按 `backend/README.md` 恢复框架依赖。根CMake默认启用Drogon，正式业务入口仍在src/main.cpp，backend健康示例只用于框架检查。

文件模式配置可从 `config/learning.example.json` 复制，启动候选程序时用 `--config config/learning.example.json`。MySQL运行配置需要在自己的机器上填写，不能使用本机私有配置。网页资源保留在static目录，运行时工作目录应为项目根。

GitHub Actions执行构建与测试，不等于持续在线部署。GitHub Pages只能发布静态前端，C++进程与数据库需要服务器。Linux测试结果以Actions实际执行记录为准。
