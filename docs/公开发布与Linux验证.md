# 公开发布与 Linux 验证

2026年11月5日，用户在当前聊天明确确认“创建公开”。公开目标为 https://github.com/yp974145-sudo/LMS4 ，没有使用监控项目仓库。

## 实际完成

- 已上传原LMS4源码、Drogon渐进迁移、阶段Word与Markdown、完整本地Git历史。
- 普通Git HTTPS上传连接被重置，改用官方Git Data API；逐个比较blob、tree与commit的SHA，确认一致后发布main。
- 对远程目录树检查，未发现数据库目录、私有配置、构建依赖、编译产物或导入占位文件。
- 全部初始168个历史blob与本机私有配置值比较，匹配0；后续新增为无密钥文档、代码与测试修正。
- 源码提交cb899c20b2f66beb250f5ed960cb909cf078f709在Ubuntu上通过基础CI和Drogon检查。

## 验证证据

基础CI：https://github.com/yp974145-sudo/LMS4/actions/runs/41985628213

Drogon Linux检查：https://github.com/yp974145-sudo/LMS4/actions/runs/41985628132

基础CI包括旧文件模式HTTP、存储安全、生命周期，以及GoogleTest单元检查；Drogon工作流编译健康入口与原主程序，并执行健康HTTP及15项业务与网页读取检查。均使用云端隔离测试数据，没有上传本机业务库。

## 云端暴露并修复的问题

1. Ubuntu的Drogon开发包未自动带齐CMake配置所需开发依赖，工作流明确列出JsonCpp、SSL及数据库客户端等开发包。
2. 旧JSON测试调用了不存在的size接口，并使用有二义性的整数零下标；改为现有as_array接口，保持原断言意义。
3. 存储锁测试误依赖Windows错误文案；改为接受锁占用含义，并增加文件内容不变断言。
4. 中文SHA256测试预期值错误，经Python hashlib独立计算核实后修正，未改加密算法。
5. 旧Socket服务Linux停止重启遇到TIME_WAIT，POSIX监听器增加SO_REUSEADDR；没有启用SO_REUSEPORT，Windows行为保留。

## 尚未完成

GitHub不是C++后端的持续在线运行环境，Pages也不能运行本项目后端或MySQL。当前只是源码托管和云端构建测试，没有公网部署。Linux验证范围不包含本项目MySQL运行、浏览器端到端、压力、长时间稳定性、TLS或多实例。用户个人掌握程度仍需独立练习验收，不把助手完成的代码当成独立贡献。
