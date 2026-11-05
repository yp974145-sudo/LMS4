# 原业务接入 Drogon

2026年11月5日。用户明确要求沿原版直接修改，不清空旧业务。本次修正上一阶段只提供独立健康入口的偏差，将成熟网络框架接入原src/main.cpp。助手实现不等于用户已独立掌握。

## 入口和范围

原CMake默认LMS_USE_DROGON=ON。原main仍负责配置、仓库、UserService和OrderService组装；启用框架后调用run_drogon。旧页面、角色、订单和仓库代码直接复用。backend健康入口仅保留为框架检查材料，不是正式业务替代品。

本轮只是网络传输替换：旧Router、JSON、JWT和MySQL访问代码仍然存在，不能宣称全部已改成Drogon原生控制器和ORM。下一步沿原模块逐项迁移，不另做空业务骨架。

## 关键代码与调用链

浏览器 → Drogon网络解析 → pre-routing advice → 框架工作队列 → 旧Router → 原接口鉴权 → 原Service → 原Repository → 转换响应交还Drogon。

include/lms/drogon_bridge.h是过渡适配层。把框架请求转换成原Request，保留原来的URL参数、请求头和JSON正文。回应时把状态码、响应头、正文交给Drogon发送；原Socket Server不会在启用框架时启动。

registerPreRoutingAdvice接收request、respond和继续路由回调。本阶段全部交给原Router，因此不调用继续路由回调，避免一次请求被处理两次。request由智能指针复制保持生命周期，respond移动到工作任务中，任务完成后调用它。

数据库和文件仓库目前仍是同步调用，不能直接放在框架I/O事件循环。这里使用Trantor提供的ConcurrentTaskQueue，不自写线程池。pending限制接收中的业务任务不超过256，满时返回503；这不是已实测吞吐指标。

main中的users和orders存活到run_drogon返回；工作队列在返回前stop并等待线程结束。CountGuard在任务离开时减少计数，异常也能释放额度。Router在工作队列结束后才销毁。RepositoryError映射500，非法参数400，其他异常500。

## 实际验证与边界

Windows原生CMake候选build/integrated-drogon/lms-server.exe已编译。tests/drogon_business.py复用原业务测试，验证登录、角色、参数边界、完整流转、重复审核和修改冲突；另读取原网页检查实际HTML内容。共15项通过，使用临时文件数据，不覆盖学习数据库。

4项自研HTTP字节级解析策略测试不在此适配测试中，它们不是业务接口契约。MySQL版集成、新浏览器端到端、Linux、压力、停机排空边界本轮未验证。候选没有替换现有日常MySQL业务exe，用户当前运行入口不会被悄悄切成文件模式。

Linux CI已加原主程序构建及业务检查，但未推送、未执行。GitHub Pages只能托管静态前端，不能运行本C++后端或MySQL。GitHub仓库地址尚未提供，没有上传。

## 面试问题与追问

1. 为什么不能删掉旧版只留健康接口？健康接口没有订单业务，删除会丢失功能。追问是否要双份维护？正式业务只在原入口演进，独立健康例子仅用于框架检查。

2. 框架已替换哪些？Drogon负责HTTP接收、解析和发送，Trantor执行同步业务任务。追问是否已使用Drogon ORM？没有，数据库访问还需下一步迁移与实测。

3. 为什么不能直接在I/O回调中查询数据库？当前仓库阻塞，会拖住事件循环。追问使用工作队列就能无限接请求吗？不能，需要限制待处理任务并明确过载反馈。

4. 回调里的引用为什么有效？服务对象属于main，run返回前停止业务线程；请求指针按值捕获延长请求寿命。追问shared_ptr会自动线程安全吗？不会，它仅管理所有权。

5. 为什么不照搬自研HTTP解析测试？框架可能采用不同的合法协议处理策略，应分别验证业务契约与框架协议行为。追问通过15项是否代表生产可用？不代表，未测项必须单独披露。

## 独立练习

沿一个POST建单请求，分别指出Drogon接收、原接口授权、Service校验和Repository写入的位置，并解释为什么旧Socket Server没有同时启动。练习未代做。
