# 现有两台电脑的安装预览

更新：2026-10-29。当前交付范围收缩为这组已配置的Mac → Windows，完成两端界面、安装和登录启动；Windows → Windows暂停在PR6草稿，Windows → Mac继续后置。这一预览使用现有输入法、音频与Siri兼容组件，不是干净机器公开发行版。

## 两端界面

主窗口默认打开“语音”，保留“键鼠连接”作为另一页。语音内部仍为“使用 / 设备 / 设置”；首页只有状态、一句操作提示和主要操作。Mac选择声源与按住说话键（F8或右Option），Windows只显示接收、已连接、正在接收和需要处理的问题，不要求Windows另接麦克风。

主窗口操作即时保存并应用语音偏好；偏好对话框继续在保存时应用，取消不影响当前后台。Windows接收角色固定，其他发送方向未在此安装预览开放。

## 后台与身份

使用`DESKFLOW_VOICE_PRODUCT=ON`构建：应用显示名和配置根为Deskflow Voice，单实例、GUI与核心本机IPC独立；Mac固定bundle ID为`org.deskflow.voice`。图标和上游署名保留。产品构建禁止复用上游安装器身份，自动更新也不会引导回官方Deskflow版本。

Windows GUI持有固定安装路径的V2子进程，用显式`--serve-v2-managed`入口。配置只引用当前用户DPAPI文件，子进程解密、加载后清除临时环境引用；凭据不通过命令行、界面或日志。`stop`或父进程stdin EOF触发取消，原生收尾仍在同一工作线程完成。正常关闭不直接杀掉接收器；收尾过慢时保留其清理，不能用进程退出假定麦克风已停止。

GUI只解析自己子进程的有限元数据，不读取输入正文或保存音频。Windows输入法RPC超时/未知所有权策略保留，状态未知时不盲目追加调用。Siri旧桥保留原有受保护配置和生命周期约束。

登录时Mac会在后台按1、2、4、8、15秒退避重连，等待Windows接收端和输入法就绪；恢复连接不自动收音、不重放按住键，下一轮必须重新按下。Windows只自动重试尚未进入监听阶段的启动失败；进入监听后异常退出必须明确恢复，避免自动重启绕过未知RPC结果的保护。停用或退出GUI会取消重试。

## Mac构建与打包

```sh
python tools/build_macos.py --qt-dir /absolute/Qt/6.11.3/macos --openssl-dir /absolute/openssl --build-dir /absolute/product-build --product --test
python tools/package_macos_preview.py --build-dir /absolute/product-build --qt-dir /absolute/Qt/6.11.3/macos --output-dir /absolute/new-preview
```

打包拒绝脏源码或已有输出目录，带入匹配的GUI/core、Qt依赖、许可与源码版本清单，验证ad-hoc签名及两个可执行文件版本。安装命令复制到当前用户的Applications，确认目标程序未运行，已有版本移到私有备份后再替换。个人配对信息不在应用或安装文件里。

独立配置准备工具只读原配置、指定的语音偏好和配对文件，复制TLS信任到新目录并设为用户私有。初次准备关闭自动核心和语音，先检查权限与界面，之后再做两端空闲切换和自启迁移；不会先破坏当前可用链路。

## 当前验证门槛

Mac产品1.27.0.23（源码7848c34）完整构建及10组明确安全CTest已通过。新增接收生命周期测试使用自己的假子进程，检查重复启动、正常停止、意外退出与销毁收尾。Windows产品1.27.0.25（源码8045f6b）已完成全新构建和4组Qt回归（35项通过、4项预期跳过、0失败）；未变的C#组件此前通过170项托管检查和DPAPI兼容检查。已配置两台设备上的独立产品及登录启动，确认键鼠TLS与语音V2就绪；Mac登录入口实际重载成功，Windows登录入口已验证复用现有实例。整机重启和新安装版真人语音输入仍需单独记录，不能据此宣称均已通过。

Windows安装使用到私有设置目录的junction。产品读取它的真实目录目标，让GUI、core和Qt配置锁使用一致路径；`voice/receiverFile`必须登记在配置保留列表，避免初始化时清理掉。以下回归复制无个人配置的公开包到临时目录，只用`--version`验证实际初始化、退出和接收路径保留，禁用核心与语音，不发送输入或录音：

```powershell
pwsh -File src/unittests/gui/TestWindowsPortableSettingsJunction.ps1 -PackagePath 'C:\path\to\unconfigured-package'
```

本地MSVC构建应在配置和构建阶段统一设置进程环境`VSLANG=1033`，让Ninja的include依赖前缀一致；既有缓存若曾漏跟踪头文件，使用全新构建目录验证。不要修改用户的系统语言。

安装验收必须另查实际固定路径、键鼠TLS、V2预检、连续输入与松开停止、自启归属和回退记录。源码、截图、构建和应用已复制，均不等于这些实际输入结果已经通过。
