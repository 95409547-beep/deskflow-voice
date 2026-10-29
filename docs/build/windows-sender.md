# Windows 麦克风发送端

日期：2026-10-29。目标是把连接到Windows控制电脑的普通麦克风送到另一台Windows，由接收电脑的输入法输入。Windows到Mac仍不在本版范围内。已有Mac到Windows真人结果单独保留，不能当作双Windows验收。

## 实现

原生Qt主GUI现在可以创建Windows收音实现，公共会话模型按实际发送平台初始化。屏幕绑定、按住键、私有核心控制、固定证书TLS、接收确认与松开结束复用既有链路。接收端仍要求通过现有预检的Windows V2组件。

Windows收音使用系统WASAPI共享模式，在独立COM线程打开所选capture endpoint；空选择明确表示使用系统默认麦克风，指定设备失效时不会回退到其他麦克风。只采集输入设备，不采集系统播放声音，不更改系统默认设备、音量或静音。

通过Windows的格式转换输出48kHz、单声道、PCM16LE，20ms一帧。工作线程只把声音加入有上限的GUI投递队列；每轮最多积压一秒声音，超过预算即停止。松开会先取消该轮投递再停止并回收收音线程，未满20ms的尾包丢弃。旧轮次的排队音频或失败通知不会进入新一轮。接收端未确认开始前仍只有有限预录，不能提前转发。

首包的discontinuity允许作为Windows启动情况；后续丢包、设备失效、隐私拒绝或连续三秒没有数据均报告失败并停止连接。重新连接只准备后台，仍需新的按下才会收音。桌面WASAPI没有与Mac相同的逐应用麦克风授权弹窗，访问拒绝时提示Windows麦克风隐私设置。

## 依赖与运行边界

- 使用已有Qt Core/Widgets/Network和Windows SDK的WASAPI、COM接口；无需增加Qt Multimedia、第三方录音库或发送端虚拟声卡驱动。
- Windows接收端的输入法和虚拟音频线依赖保持原计划，由安装与配对阶段统一检测、解释和处理，不把第三方驱动直接合入源码包。
- 当前键鼠控制端必须由GUI启动桌面核心，才能取得该子进程的私有控制凭据。Windows系统服务模式尚未接入语音控制，不能视为默认安装即用。
- 当前仍用开发配对文件绑定一台Windows目标，正式发现、配对向导和自动启动迁移未完成。设备角色选择“发送声音”；单独选择“接收语音”不会启动发送采集，接收仍由Windows组件承担。
- 本批只构建、测试，不替换正在使用的Mac或Windows程序，不自动打开真实麦克风。

## 检查

`AudioCaptureQueueTests`使用内存假音频覆盖不整齐的包边界、静音、释放后延迟通知、队列积压和下一轮恢复，不录音、不发键。`VoiceBackendTests`在回环TLS中分别用Mac与Windows会话身份完成连续四轮开始确认、发送、结束与下一轮。

Windows额外构建`AudioCaptureWindowsTests`，只传入随机生成的不存在的设备ID，检查异步失败、不会回退到默认设备、连续重试与初始化中松开后的清理。该套件不使用空设备ID，不打开麦克风，不安装按键钩子。

Mac完整构建及安全检查包含`AudioCaptureQueueTests`；Windows构建和实际收音结果另在验证JSON记录。编译、模拟会话与不存在设备检查不等于真实Windows录音或跨机输入。

## 双Windows实机验收

准备隔离的发送端候选和回退副本，避免同时运行两套Deskflow控制核心。发送电脑连普通麦克风并作为键鼠控制端，目标电脑运行已配对的Windows V2接收端。先用F8，不依赖平台之间的右Alt模拟。

依次验证两句连续输入、松开停止、按住时返回发送电脑停止、释放后再按恢复；随后检查空闲重开、网络断开和恢复、按住中断、输入位置变化。文字由用户在实际输入位置确认，只采集会话和错误计数，不读取正文、不保存音频。第二台Windows已确认可用，尚未纳入当前远程开发环境，不能填写为已通过。

WASAPI参数与缓冲处理依据Microsoft的[Initialize](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-initialize)、[流标志](https://learn.microsoft.com/en-us/windows/win32/coreaudio/audclnt-streamflags-xxx-constants)和[GetBuffer](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudiocaptureclient-getbuffer)说明。这里只记录实现选择，设备兼容性仍以实机结果为准。
