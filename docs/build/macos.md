# 从完整源码构建 Mac 开发版本

这份说明用于工程基础验证，不会安装或启动应用。需要 Apple 命令行开发工具、Python 3.13，以及下述固定版本 SDK。普通用户未来使用安装包，不需要这些开发工具。

## 工具

在源码根运行，虚拟环境和构建缓存位于用户缓存目录，不进入源码，也不依赖原型的临时文件夹：

```sh
python3 -m venv "$HOME/Library/Caches/DeskflowVoice/build-tools"
"$HOME/Library/Caches/DeskflowVoice/build-tools/bin/python" -m pip install -r tools/build-requirements.txt
"$HOME/Library/Caches/DeskflowVoice/build-tools/bin/aqt" -c tools/aqt.ini install-qt mac desktop 6.11.3 clang_64 --outputdir "$HOME/Library/Caches/DeskflowVoice/Qt" --archives qtbase qtdeclarative qtsvg qttools qttranslations --timeout 120
```

Qt 安装器的可选模块和基础 archive 列表不同；本基线所需 qtsvg/qttools 属于基础 archive，不使用不存在的同名可选模块参数。Qt 6.11.3 的 lupdate 依赖 QtQml，因此需要 qtdeclarative archive。网络下载中断后需重新检查 SDK 完整性，不能把部分安装当作已就绪。

## OpenSSL

固定 OpenSSL 3.6.4，从 [官方 Release](https://github.com/openssl/openssl/releases/tag/openssl-3.6.4)取得源码及 SHA-256 文件，校验后解压。以下命令在解压后的源码目录运行，目标为本机 ARM64 与 macOS 15：

```sh
./Configure darwin64-arm64 no-shared no-tests --prefix="$HOME/Library/Caches/DeskflowVoice/openssl/3.6.4-macos15" -mmacosx-version-min=15.0
make -j8
make install_sw
```

不能用在更新系统目标上编译的 Homebrew 静态库，来证明较低 macOS 的支持。这个源码构建用于消除旧依赖目标版本不一致警告；真正支持 macOS 15 仍需在该版本机器上安装和运行验收。

## 构建与有限检查

在产品源码根运行：

```sh
"$HOME/Library/Caches/DeskflowVoice/build-tools/bin/python" tools/build_macos.py --qt-dir "$HOME/Library/Caches/DeskflowVoice/Qt/6.11.3/macos" --openssl-dir "$HOME/Library/Caches/DeskflowVoice/openssl/3.6.4-macos15" --test
```

脚本构建 GUI、core、公共模型与测试，只运行公共会话、右 Option 与 TIS 相关的有限检查，不打开 Deskflow 主界面、注册键鼠捕获、读写系统剪贴板或启动语音。

`build/macos` 中的应用仍属于未完成产品身份/安装器迁移的开发产物，不提供替换现有安装的步骤。Windows 完整构建及干净安装按阶段计划另行验证；本说明不声称跨平台安装包已完成。
