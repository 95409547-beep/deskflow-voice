#!/bin/zsh
# SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
# SPDX-License-Identifier: MIT
set -euo pipefail
payload="${0:A:h}/Deskflow Voice.app"
destination="$HOME/Applications/Deskflow Voice.app"
if [[ ! -d "$payload" || -L "$payload" || -L "$destination" ]]; then
  print '未找到完整安装文件，或安装位置是链接。'
  exit 1
fi
identifier=$(/usr/libexec/PlistBuddy -c 'Print CFBundleIdentifier' "$payload/Contents/Info.plist")
if [[ "$identifier" != 'org.deskflow.voice' ]]; then
  print '这不是 Deskflow Voice 安装包。'
  exit 1
fi
/usr/bin/codesign --verify --deep --strict "$payload"
if /bin/ps -ax -o comm= | /usr/bin/grep -F -x -q "$destination/Contents/MacOS/Deskflow"; then
  print '请先退出正在运行的 Deskflow Voice，再安装。'
  exit 1
fi
mkdir -p "$HOME/Applications"
temporary=$(/usr/bin/mktemp -d "$HOME/Applications/.deskflow-voice-install.XXXXXX")
previous="$temporary/previous.app"
finished=false
cleanup() {
  if [[ "$finished" != true && -d "$previous" && ! -e "$destination" ]]; then
    /bin/mv "$previous" "$destination"
  fi
  /bin/rm -rf "$temporary"
}
trap cleanup EXIT
/usr/bin/ditto "$payload" "$temporary/Deskflow Voice.app"
/usr/bin/codesign --verify --deep --strict "$temporary/Deskflow Voice.app"
if [[ -e "$destination" ]]; then
  backup_root="$HOME/Library/Application Support/DeskflowVoice/install-backups"
  /bin/mkdir -p "$backup_root"
  /bin/chmod 700 "$backup_root"
  backup="$backup_root/Deskflow Voice-$(/bin/date +%Y%m%d-%H%M%S)-$(/usr/bin/uuidgen).zip"
  /usr/bin/ditto -c -k --sequesterRsrc --keepParent "$destination" "$backup"
  /bin/chmod 600 "$backup"
  /usr/bin/unzip -tq "$backup" >/dev/null
  /bin/mv "$destination" "$previous"
fi
/bin/mv "$temporary/Deskflow Voice.app" "$destination"
finished=true
print 'Deskflow Voice 已安装到“应用程序”。'
print '已有连接和登录启动由本次迁移配置；安装文件本身不包含个人配对信息。'
