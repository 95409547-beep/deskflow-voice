# 第一批工程验证记录

日期：2026-10-27。范围：公共会话模型、Mac 右 Option/TIS 补丁、整理后的 Windows 接收/输入法组件及构建入口。对应开发 PR：[工程基础 #1](https://github.com/95409547-beep/deskflow-voice/pull/1)。

后续更正：本记录的旧 `OSXKeyStateTests` 包含上游真实发键 case；不能把它整体视为无系统输入检查。当前入口已改成 15 个明确不发键的 `OSXKeyStateSafeTests`，复测结果与原测试失败记录见[漏松键恢复](modifier-recovery.md)。

## 本地验证

| 项目 | 环境与方式 | 结果 |
|---|---|---|
| 独立公共会话 | C++20，CMake/CTest，不使用 Qt 或设备 | 通过；两个发送平台共 40 个模拟回合及异常收尾 |
| Mac 完整源码 | macOS 27.2 ARM64，Apple Clang 17，Qt 6.11.3，OpenSSL 3.6.4 源码静态构建，部署目标 15.0 | GUI/core/检查程序编译通过，未安装或启动 Deskflow |
| Mac 有限回归 | `tools/build_macos.py --test` | VoiceSessionTests、OSXKeyStateTests、TISMainThreadProbe、TISReadPathsTests、TISQtRuntimeProbe 全部通过 |
| Windows 组件源码编译 | Mac 上 .NET SDK 10.0.401 Release | 通过，0 警告、0 错误；未运行 Windows 原生调用 |
| 默认与旧命令入口 | Mac 上运行已编译的组件 CLI | 默认/帮助退出 0，旧实时监听参数退出 2 |
| Windows 准备检出 | Windows x64，.NET 10.0.400，PowerShell 7.6.5 | 独立发布及 82 项纯托管检查通过，见[组件记录](../../components/windows-voice/M1-VALIDATION.md) |
| 格式与许可元数据 | clang-format 20.1.0、REUSE | 通过；不替代第三方组合和分发许可审查 |
| 公开源码筛选 | 暂存文件、来源清单、文档链接 | 无个机路径、私有凭据、原生二进制或生成产物进入提交 |

Qt 初次最小 SDK 缺少 `lupdate` 需要的 QtQml，补齐 `qtdeclarative` archive 后完整构建与有限回归通过。依赖清单与构建说明已同步。

## CI 与结果归属

`Voice foundation` 工作流检查 Mac、Windows、Linux 上的公共会话；Windows 另构建组件、运行纯托管检查并验证独立运行库发布。保留上游完整 Deskflow 构建工作流。具体结果应查看 PR 当前提交的 Checks，旧提交通过不能替代新提交结果。

Linux CI 仅验证通用代码；Windows ARM64 的继承构建也不等于已取得首版硬件支持。Windows 基础组件暂以 x64 发布检查为准。Mac 15 只是本次编译目标，尚未完成该系统版本上的安装和实际运行验收。

## 尚未证明的能力

公共 C++ 模型和 C# 组件还未统一协议或连接到真实语音服务。没有产品语音分页、普通麦克风发送端、设备配对、完整安装器、独立产品身份或签名发行包。Windows → Windows 尚无实机语音验收；第二台 Windows 已确认可用。

原型链路此前的 Mac → Windows 真人验收仍属于原型部署。本次只构建独立开发产物，未替换运行程序、驱动、证书、默认声源或登录自启设置。
