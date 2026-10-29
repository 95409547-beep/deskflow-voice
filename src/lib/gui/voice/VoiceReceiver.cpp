/*
 * SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "VoiceReceiver.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace deskflow::gui::voice {
VoiceReceiver::VoiceReceiver(QObject *parent, QString executable)
    : QObject(parent),
      m_process(new QProcess(this)),
      m_executable(std::move(executable))
{
  if (m_executable.isEmpty())
    m_executable = QCoreApplication::applicationDirPath() + QStringLiteral("/voice/deskflow-windows-voice.exe");
#ifdef Q_OS_WIN
  m_process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
    args->flags |= CREATE_NO_WINDOW;
  });
#endif
  connect(m_process, &QProcess::readyReadStandardOutput, this, &VoiceReceiver::read);
  // Diagnostic output stays private to the child; never forward installation details into the UI.
  connect(m_process, &QProcess::readyReadStandardError, this, [this] { m_process->readAllStandardError(); });
  connect(m_process, &QProcess::errorOccurred, this, [this] {
    if (!m_stopping)
      setStatus(tr("接收服务无法启动，请检查安装"));
  });
  connect(m_process, &QProcess::finished, this, [this] {
    m_connected = m_recording = false;
    m_lines.clear();
    if (!m_stopping)
      setStatus(tr("接收服务已停止，请重新开启"));
  });
  setStatus(tr("语音接收未开启"));
}
VoiceReceiver::~VoiceReceiver()
{
  if (!stop()) {
    // Leave a slow native cleanup alive. Its stdin is closed, so the managed
    // receiver finishes its owned capture; QProcess destruction must not kill it.
    m_process->disconnect(this);
    m_process->setParent(nullptr);
    connect(m_process, &QProcess::finished, m_process, &QObject::deleteLater);
  }
}
bool VoiceReceiver::running() const
{
  return m_process->state() == QProcess::Running;
}
void VoiceReceiver::setStatus(const QString &status)
{
  m_status = status;
  Q_EMIT changed();
}
void VoiceReceiver::start(const QString &config)
{
  if (m_process->state() != QProcess::NotRunning && m_config == config)
    return;
  if (!stop())
    return;
  m_config = config;
  if (!QFileInfo::exists(m_executable) || !QFileInfo::exists(config)) {
    setStatus(tr("请先完成接收端安装与配对"));
    return;
  }
  m_stopping = false;
  setStatus(tr("正在准备语音接收"));
  m_process->setProgram(m_executable);
  m_process->setArguments({QStringLiteral("--serve-v2-managed"), config});
  m_process->start();
}
bool VoiceReceiver::stop()
{
  m_stopping = true;
  m_connected = false;
  if (m_process->state() != QProcess::NotRunning) {
    // EOF also stops a managed receiver if the GUI crashes.
    m_process->write("stop\n");
    m_process->closeWriteChannel();
    if (!m_process->waitForFinished(5000)) {
      setStatus(tr("正在等待输入法结束，请稍候"));
      return false;
    }
  }
  m_lines.clear();
  m_recording = false;
  setStatus(tr("语音接收未开启"));
  return true;
}
void VoiceReceiver::read()
{
  m_lines += m_process->readAllStandardOutput();
  if (m_lines.size() > 65536) {
    stop();
    setStatus(tr("接收状态异常，请重新开启"));
    return;
  }
  while (m_lines.contains('\n')) {
    const auto length = m_lines.indexOf('\n');
    const auto line = m_lines.first(length);
    m_lines.remove(0, length + 1);
    const auto details = QJsonDocument::fromJson(line).object().value(QStringLiteral("details")).toObject();
    const auto type = details.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("listening"))
      setStatus(tr("接收已开启，等待 Mac 连接"));
    else if (type == QStringLiteral("authenticated")) {
      m_connected = true;
      setStatus(tr("已连接 Mac，可以开始说话"));
    } else if (type == QStringLiteral("started") || type == QStringLiteral("native_started")) {
      m_recording = true;
      setStatus(tr("正在接收语音"));
    } else if (type == QStringLiteral("ended")) {
      m_recording = false;
      setStatus(
          details.value(QStringLiteral("ready")).toBool() ? tr("已结束，可以继续下一句") : tr("请检查输入法后重新连接")
      );
    } else if (type == QStringLiteral("client_closed")) {
      m_connected = false;
      setStatus(m_recording ? tr("连接已断开，正在结束本轮") : tr("接收已开启，等待 Mac 连接"));
    } else if (type == QStringLiteral("native_ended") || type == QStringLiteral("native_already_ended")) {
      m_recording = false;
      if (!m_connected)
        setStatus(tr("接收已开启，等待 Mac 连接"));
    } else if (type == QStringLiteral("preflight_rejected") || type == QStringLiteral("error")) {
      m_recording = false;
      setStatus(tr("接收未就绪，请检查输入法与声音组件"));
    }
  }
}
} // namespace deskflow::gui::voice
