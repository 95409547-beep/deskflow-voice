# GitHub 工程与发布方案

版本：1.0 · 2026-10-27。仓库与基础开发已启动；以下目录、UI、安装与发布能力仍按阶段推进，未实现项不能当作交付。入口：[文档总览](README.md)。

## 1. 仓库组织与身份

以 [deskflow/deskflow](https://github.com/deskflow/deskflow) 为上游，在用户选定的账号/组织下建立 fork。保留 upstream 远程，产品默认分支维护可发布代码，功能工作通过短期分支与 PR 合并。仓库归属、对外名称与正式应用 ID 在 P-001 固定；本方案以“Deskflow 跨设备语音”作为说明名称，不代表已注册品牌或已创建仓库。

产品使用独立的应用标识、安装目录、配置根、Windows UpgradeCode/服务标识和更新通道，保留上游署名、许可与来源。升级不能误卸载官方 Deskflow，不能共用上游发布签名或更新地址。官方版可保留安装，但同时占用键鼠监听/控制时要检测冲突并提示选择运行实例。

分支建议：`main` 保持已验收产品版本；`feature/voice-core`、`feature/windows-sender`、`feature/voice-ui` 等作为拟定开发分支；`release/1.x` 仅在确有维护需求时建立。版本 Tag 触发发布，测试构建使用预发布标记。分支名是设计建议，本轮未执行创建。

## 2. 目录设计

保留上游目录，新增能力集中放置，避免长期复制整套 Deskflow。目录树为未来 fork 的建议结构；它不是当前工作区已经存在的目录清单。

```text
repo/
├── src/
│   ├── apps/
│   │   ├── deskflow-gui/             # 保留上游 GUI 入口
│   │   ├── deskflow-core/            # 保留键鼠核心；提供本机事件接口
│   │   └── voice-service/            # 新增用户态后台入口
│   ├── lib/
│   │   ├── gui/voice/               # 语音分页、首次向导、测试与诊断
│   │   └── voice/
│   │       ├── model/               # 设备、会话、能力、设置与状态
│   │       ├── ipc/                 # GUI/core/后台的受控本机通信
│   │       ├── transport/           # 配对、TLS、版本协商、重连
│   │       ├── audio/               # 格式转换、有界队列、发送/播放
│   │       ├── capture/macos/       # 普通系统麦克风
│   │       ├── capture/windows/     # Windows 麦克风与设备变化
│   │       ├── triggers/            # 物理快捷键与遥控事件适配
│   │       └── adapters/windows/    # 虚拟音频与输入法控制接口
│   └── unittests/voice/             # 公共状态/协议/音频异常检查
├── components/
│   ├── windows-voice/               # 保留 C# 时的自包含后台工程
│   └── siriremote-connector/        # 许可审查后的独立设备连接组件
├── deploy/
│   ├── windows/                    # 安装、修复、登录任务、卸载
│   └── mac/                        # App/PKG/DMG、权限、可选设备组件
├── tests/
│   ├── integration/                # 跨进程、网络和依赖故障
│   ├── acceptance/                 # 两方向真人与干净安装规程
│   └── fixtures/                   # 合成测试数据，不含真实语音
├── docs/
│   ├── product/                    # 范围、路线与用户说明
│   ├── architecture/               # 协议、接口和迁移说明
│   ├── adr/                        # 架构/许可/设备选型决策
│   └── support/                    # 安装、诊断、兼容清单
├── third_party/                    # 来源、许可、哈希与分发清单
├── tools/                          # 构建、打包、验证脚本
└── .github/
    ├── workflows/                  # 检查、双平台构建、签名发布
    └── ISSUE_TEMPLATE/             # 兼容问题和安装问题模板
```

`components/siriremote-connector` 在许可裁定后只能放允许分发的代码、接口或获取说明；不会因存在这个目录就自动取得混用 GPL 代码的权利。需要独立仓库时，在主仓记录固定版本、来源和边界，不直接复制整个 GPLv3 工程进 Deskflow 源码。

## 3. 从现有实现迁移

| 现有来源 | 未来归属 | 迁移要求 |
|---|---|---|
| Mac `bridge/mac/sender.py`、`ring_reader.c` | transport/audio、Siri connector | 保留只在物理按下后采集、重连不重放和专用来源检查；去个人路径 |
| Mac `route.py` 与 Deskflow 高级命令 | core 本机事件接口、路由状态 | 消除手工命令和读取日志文本的依赖 |
| `mac-server-option-and-tis-r4.patch` | 平台源码与测试 | 在统一 Qt 工具链上迁移；左右按键与主线程访问分开评审 |
| Windows `Receiver.cs`、`Start-Receiver.ps1` | windows-voice、audio/adapters | 收齐源码和构建入口；前台逐轮绑定与有界缓冲保留 |
| Windows `rpc-probe` 中控制代码 | 输入法适配器与服务 API | 消除实验窗口限制、固定服务 PID 和模拟右 Alt 串联 |
| 两端自启管理脚本 | 部署与生命周期模块 | 保留依赖等待、单实例、恢复语义；绑定实际安装位置与用户身份 |
| 验收 JSON、历史联调说明 | 内部证据与脱敏测试报告 | 原始个人记录留在内部，公开报告不带令牌、路径、窗口正文 |

当前根目录不是 Git 仓库；`sources/deskflow` 为上游 v1.27.0 提交 `081f6478e654a112168879e67a41ce7d7e4ae3af` 的有改动检出。M1 先保留这些改动的可恢复快照，再在选定上游稳定基线中逐项迁移，不能直接覆盖或重置现有目录。

Windows 主要工程在另一台电脑的工作区。本地缺少完整 Windows 源码归档是明确缺口：必须收齐 C#/PowerShell/native 辅助代码、项目文件、锁定文件、构建与测试入口、部署资源及许可证。只复制已编译 DLL、运行配置或最终日志不满足公开源码要求。

## 4. 构建与运行依赖分离

构建清单固定 Qt、编译器、CMake、TLS/音频库和可选 .NET 发布工具版本。版本值在 M0/M1 根据实际可构建结果写入锁定文件；不会沿用未经验证的“最新版本”。GUI/core/后台使用兼容的一组依赖，不以混合旧应用包作为发行构建。

Windows 输入逻辑可先保留 C#，采用包含运行时的自包含产物，通过本机 IPC 接入；用户不安装 SDK 或 PowerShell 开发环境。若仍需要 PowerShell，仅作为受产品安装器管理的有限部署入口，运行时来源和许可必须清楚，不能引用 Codex 缓存中的解释器。

Mac 后台编译为随 App 分发的受签名组件，通用普通麦克风采集不依赖 Python。必要的独立设备组件按其许可和签名方式交付。首版主链路不要求用户安装 Xcode；Siri 增强模块的 PacketLogger 获取是单独明确的例外。

## 5. CI 与公开包

复用上游现有平台工作流与安装资源，逐步增加以下关卡；上游现有打包基础见 [Mac 部署源码](https://github.com/deskflow/deskflow/blob/master/deploy/mac/deploy.cmake)和 [Windows 部署源码](https://github.com/deskflow/deskflow/blob/master/deploy/windows/deploy.cmake)。

| 触发 | 检查/构建内容 | 产物与限制 |
|---|---|---|
| 每个 PR | 格式、编译、会话/协议/异常检查、依赖与敏感信息检查 | 构建工件；不接触正式签名凭据、不自动发布 |
| 合并到主线 | Windows/Mac 完整构建、安装静态检查、组件版本清单 | 内部预览包、源码提交和哈希 |
| 版本 Tag | 重新核对门槛、签名、公证、校验安装产物 | GitHub Release、源码、包、哈希、变更说明、兼容清单 |
| 真人发布验收 | 物理麦克风、两端输入、重启、睡眠与恢复 | 记录在 acceptance 报告中；不以 CI 合成检查替代 |

首批发布候选为 Windows x64 安装包、Mac ARM64 安装包；正式支持前采集 Windows 的实际体系结构与系统矩阵。其他架构的构建可作为实验工件，只有完成安装/设备验证后才转为正式包。

Windows 采用 MSI 为主，确有多依赖安装需要时由签名 EXE 安装引导器编排。Mac 基础 App 用 DMG 交付，系统级可选组件用独立 PKG；最终文件名、产品 ID 和签名归属在 P-001 固定。每个平台用户只需一个主入口，可选组件由向导按来源安装。

正式 Mac 公开发行走 Developer ID 签名与公证流程；Windows 发布文件使用有效签名并验证安装器与子组件，不能承诺新签名包立刻不会出现 SmartScreen 提示。[Apple 说明](https://developer.apple.com/developer-id/)、[Microsoft 说明](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation)。签名凭据放发布环境密钥存储，不写入仓库或日志。

## 6. 版本、兼容与更新

产品版本、上游基线、语音协议版本、设备组件版本与豆包兼容配置分别记录。连接时协商发送/接收能力与协议；旧版本或官方 Deskflow 对端没有语音能力时，页面显示不支持语音，键鼠保持原行为，不发送旧端无法理解的新消息。

首版只检查更新并引导用户安装签名包，不启用无人确认的自动覆盖升级。升级前等待录音结束，保留可回退的配置版本；升级失败恢复上一可用版本。后续静默更新需另行设计组件原子替换与后台版本一致性。

每份 Release 至少包含支持矩阵、上游基线、源代码获取方式、安装/卸载说明、依赖及许可清单、已知问题、包哈希与测试报告链接。驱动或组件不再需要时仍按共享依赖规则处理，不能凭“本次安装过”就自动删除。

## 7. 默认不进入公开仓库的内容

设备证书私钥、令牌、PFX/签名密码、真实用户音频、输入正文、个人配置、绝对用户名路径、内网拓扑和完整原始联调日志均不进入公开源代码或安装包。公开示例使用运行时生成的设备身份和从标准用户目录推导的路径，不提供真实凭据样例。

产品首次启动生成本机身份，私密数据使用系统凭据存储和适当的文件访问控制；新安装之间不共享预置配对密钥。诊断包单独受用户确认并默认脱敏。

## 8. 最先建立的开发文档

M1 至少提供从干净源码构建两个平台的说明、协议与本机 IPC 版本定义、依赖 manifest、Siri 组件许可裁定、豆包支持矩阵、配置迁移规则和两方向验收入口。上游修复与产品功能分开提交，便于将通用键盘/TIS 修复独立贡献上游。
