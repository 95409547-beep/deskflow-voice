# 漏松键恢复：纳入产品源码

日期：2026-10-27。来源：同工作区的“排查 Codex 任务新窗口行为”任务所做的修复。本批完成源码评审、构建检查、测试入口修正和 Git 归档，不再替换或重启运行程序。

## 问题与处理

Windows 曾持续认为左 Shift、Ctrl、Win 按下，用户分别按下并松开后，点击意外新开窗口的现象消失。现有证据支持异常修饰键状态参与故障，最初漏松键的来源仍未完全确定。

Mac 控制端新增 `OSXKeyState::releaseStaleModifiers`：在下一次鼠标、滚轮或键盘输入进入队列前，对比登记的按键与事件/HID 状态，仅为已经不再持有的修饰键补发松开。

- 覆盖左右 Shift、Control、Option、Command，保留右 Option → AltGr 兼容映射。
- 不制造新的按下，不按计时强制释放，保留仍实际持有的组合键与拖选。
- Caps Lock、Num Lock、普通字符键不属于这次恢复范围。
- 这是内核自动恢复，不在语音首页增加设置开关。

原始修复补丁 SHA256：`1a62a0ac02fe3c64ee593e67c822c93cad14f81f0869d08ca17d8cceaa1049a5`。本批确认产品工作区已完整应用该补丁，再统一格式；原始运行报告和回退副本继续留在内部工作区。测试版已由原任务部署，这次只读核对安装核心的哈希与该部署记录一致。

## 本批验证

| 检查 | 结果与边界 |
|---|---|
| 锁定 Qt 6.11.3/OpenSSL 3.6.4 的产品完整构建 | 通过；开发产物，未部署 |
| 15 个不发键用例 | 通过，加初始化/收尾为 17 passed；只构造对象、翻译事件、检查内存队列 |
| 5 组有限 CTest | VoiceSessionTests、OSXKeyStateSafeTests、TISQtRuntimeProbe、TISMainThreadProbe、TISReadPathsTests 全部通过 |
| Mac 状态检查 | HID 与会话状态下左右 Shift/Ctrl/Option/Command 八键均未按下 |
| Windows 状态检查 | 真实交互桌面短时连续 16 次采样，左右 Shift/Ctrl/Alt/Win 八键均未按下；只读核对 |
| 长时日常复发、全部快捷键与整机重启 | 本批未验收；不能据此认定最初原因已经完全消除 |

## 修正开发检查入口

复测发现旧 `--test` 入口把上游完整 `OSXKeyStateTests` 当作安全检查，实际包含会发键的 `fakePollShift`、`fakePollChar`、`fakePollCharWithModifier`。本次该完整套件结果为 19 passed、1 failed（`fakePollShift` 的右 Shift 检查）；这个失败结果保留，不写成全量通过，也不继续重复发键。

现在新增显式列出 15 个用例的 `OSXKeyStateSafeTests`；构建脚本改用它，完整的真实发键测试默认停用，仅在专用交互环境显式开启 `RUN_INTERACTIVE_INPUT_TESTS`。后续不能用此选项绕过正常开发的无发键边界。

此前工程基础记录中 `OSXKeyStateTests` 指完整套件，应与当前的安全回归区分。修饰键检查未发现本次复测后的残留；没有向 Windows 注入修复键或改系统配置。
