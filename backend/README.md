# LMS4 Drogon 后端迁移

阶段 1：使用成熟 HTTP 框架建立 Windows、Linux 共用的 C++17 入口。现有订单业务与网页尚未迁入；旧 `START-MYSQL.cmd` 继续运行原业务。新入口不是独立的新业务项目，而是原 LMS4 网络层的迁移落点。

## 当前验证范围

- Windows：MinGW GCC 15.2.0、Drogon 1.9.14、Kitware CMake 3.31.6，实际编译和 CTest HTTP 检查通过。
- 测试：随机端口启动，健康接口返回 200 与 windows 标记，未知接口 404，4 个非法端口拒绝启动。日志和测试工作目录自动清理。
- Linux：预设、构建脚本及 Ubuntu 24.04 CI 已编写；本机 WSL 未安装，没有实际 Linux 运行结果，未触发远程 CI。
- 框架依赖带有数据库客户端，不代表本阶段连接了数据库。健康接口仅表示进程能响应。

## Windows

官方依赖的包名、来源和 SHA256 在 `dependencies-windows.lock.json`。运行下列命令恢复到 E 盘项目依赖目录：

```powershell
python -B backend/setup_windows.py
powershell -NoProfile -ExecutionPolicy Bypass -File backend/build-windows.ps1
```

需要已有 `C:\msys64\ucrt64\bin\g++.exe`、Python 和支持 zstd 的 Windows tar。依赖不写入系统 MSYS2。正式构建产物位于 `build/backend-windows`；一次性下载、编译临时文件和 HTTP 工作目录位于 `C:\Users\23974\Desktop\jk\x` 的任务专属子目录。

双击根目录 `START-BACKEND.cmd`，访问 `http://127.0.0.1:18082/api/health`。此阶段只提供 JSON 接口，没有新网页。按 Ctrl+C 停止。

## Linux 待验证入口

Ubuntu 24.04 可使用其官方 universe 仓库中的 libdrogon-dev（1.8.7），与 Windows 验证版本不同，兼容性需要实际检查：

```bash
sudo apt-get update
sudo apt-get install -y g++ cmake ninja-build python3 libdrogon-dev
bash backend/build-linux.sh
./build/backend-linux/lms-backend --port 18082
```

Linux 默认仅监听回环地址，不能直接从其他电脑访问。上线部署、反向代理、TLS、数据库就绪探测与服务管理不属于本阶段完成项。

## 调用链

`main → registerController → Drogon 接收请求 → HealthController::get → HealthService::check → JsonCpp 响应 → callback`。

下阶段优先迁移认证与只读订单查询，继续复用已有订单模型和业务规则，逐步替换自研 HTTP、JSON、数据库访问能力。Redis、消息队列和 RPC 仍需分阶段接入。
