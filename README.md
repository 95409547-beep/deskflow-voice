# Deskflow Voice

Deskflow 的跨设备语音开发分支。首版目标是 **Mac → Windows、Windows → Windows**，Windows → Mac 留给后续版本。

当前是产品开发阶段，**还没有可供普通用户安装的完整语音发行版**。Mac普通USB麦克风已在独立开发窗口、主GUI全局F8两种入口分别通过连续两句真人输入与松开停止；切屏、退出和重连回归仍需分别完成。Windows → Windows 发送端尚未接入。

## 已落地

- 基于 Deskflow v1.27.0 固化独立开发目录，保留上游历史与版权。
- 迁移 Mac 右 Option、TIS 主线程访问与漏松键自动恢复修复及对应检查。
- 新增可独立构建的 C++20 会话状态机：两种发送端、Windows 接收端、重复会话、松开、焦点变化、断线与迟到消息处理。
- 新增跨平台会话 CI，以及 Windows 组件构建、纯托管检查和独立运行库发布检查。
- 迁移 Windows 接收与输入法适配源码；默认显示帮助，显式V2开发入口已接通，166项纯托管检查通过。
- 两端原型源码已作内部快照；公开组件已移除个机路径、设备标识、凭据和运行记录。
- 新增 v0.2 离线交互原型：“使用 / 设备 / 设置”分页、按需开始、问题修复与连续输入演示。
- 原生 Qt 设置已接入分页、配置保存、只读麦克风枚举与结构化键鼠连接状态；主GUI现已绑定后台，只有私有控制和接收端都准备好才显示就绪。
- 新增普通麦克风开发链路：CoreAudio、Qt固定证书TLS、Windows V2直接输入法控制和原生按住试说；已通过普通USB麦克风连续两句真人输入与松开停止。
- 新增Deskflow核心按住键与屏幕路由，覆盖切屏取消、退出收尾、租约和重连不续录；构建及回环测试通过，全局F8已通过连续两句真人输入与松开停止，其他生命周期与正式安装尚待实机验收。

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
- [漏松键恢复与安全检查入口](docs/build/modifier-recovery.md)
- [原生设置、设备检测与布局检查](docs/build/native-settings/README.md)
- [真实语音后台的开发入口与验证边界](docs/build/voice-runtime.md)
- [Deskflow屏幕路由、全局键与主GUI接线](docs/build/voice-routing.md)
- [Windows 独立组件构建](components/windows-voice/README.md)
- [本批工程验证记录](docs/build/foundation-validation.md)
- [语音界面交互原型与设计说明](docs/prototypes/cross-device-voice/README.md)
- [上游 Deskflow 说明](.github/README.md)

当前代码不应替换已在使用的 Deskflow。构建与有限检查不自动安装、不启动核心，设置检查使用临时配置。独立产品标识、安装器和语音服务将在相应阶段实现。

## 来源与许可

本项目基于 [deskflow/deskflow](https://github.com/deskflow/deskflow)。保留 `LICENSE`、`LICENSES/`、`REUSE.toml` 与文件级许可。新增会话代码按 GPL-2.0-only WITH LicenseRef-OpenSSL-Exception 提供，构建脚本使用 MIT。

没有将 SiriRemoteForge 的 GPLv3 源码、PacketLogger、豆包二进制或 VB-CABLE 驱动合并进本仓库。相关设备与输入法支持的分发边界仍按产品计划处理。
