# Deskflow Voice

Deskflow 的跨设备语音开发分支。首版目标是 **Mac → Windows、Windows → Windows**，Windows → Mac 留给后续版本。

当前是工程基础阶段，**还没有可供普通用户安装的完整语音发行版**。现有原型曾通过 Mac → Windows 真人输入验收；本仓库新增的公共会话模块通过的是纯状态检查，不能据此宣称 Windows → Windows 已经可用。

## 已落地

- 基于 Deskflow v1.27.0 固化独立开发目录，保留上游历史与版权。
- 迁移 Mac 右 Option 与 TIS 主线程访问修复及对应检查。
- 新增可独立构建的 C++20 会话状态机：两种发送端、Windows 接收端、重复会话、松开、焦点变化、断线与迟到消息处理。
- 新增跨平台会话 CI；没有录音、网络传输、驱动安装或输入注入。
- 两端原型源码已作内部快照。带有个机配置的原始 Windows 快照不会直接发布，正在整理成可复用组件。

## 检查公共会话模块

只需要 CMake 3.24+ 和 C++20 编译器，不需要 Qt、麦克风或输入法。这些命令不会注册全局按键、录音或启动 Deskflow：

```sh
cmake -S src/lib/voice -B build/voice -DBUILD_TESTING=ON
cmake --build build/voice --config Release
ctest --test-dir build/voice -C Release --output-on-failure
```

## 开发说明

- [范围与开发计划](docs/product/README.md)
- [会话接口与边界](docs/architecture/voice-session.md)
- [首批技术与许可决策](docs/adr/0001-foundation.md)
- [Mac 完整源码构建](docs/build/macos.md)
- [上游 Deskflow 说明](.github/README.md)

当前代码不应替换已在使用的 Deskflow。开发构建不自动安装、不启动核心、不修改已有配置。独立产品标识、安装器和语音页面将在相应阶段实现。

## 来源与许可

本项目基于 [deskflow/deskflow](https://github.com/deskflow/deskflow)。保留 `LICENSE`、`LICENSES/`、`REUSE.toml` 与文件级许可。新增会话代码按 GPL-2.0-only WITH LicenseRef-OpenSSL-Exception 提供，构建脚本使用 MIT。

没有将 SiriRemoteForge 的 GPLv3 源码、PacketLogger、豆包二进制或 VB-CABLE 驱动合并进本仓库。相关设备与输入法支持的分发边界仍按产品计划处理。
