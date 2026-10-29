/*
 * SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "VoiceService.h"
#include "common/Settings.h"
#include "gui/core/CoreProcess.h"
#include <QFileInfo>
#include <QStandardPaths>
#include <algorithm>

namespace deskflow::gui::voice {
VoiceService::VoiceService(CoreProcess &core, QObject *parent, AudioCapture *capture)
    : QObject(parent),
      m_core(core),
      m_backend(this, capture),
      m_routing(m_backend, this)
{
  connect(&core, &CoreProcess::voiceEndpointChanged, this, &VoiceService::endpointChanged);
  m_retry.setSingleShot(true);
  connect(&m_retry, &QTimer::timeout, this, &VoiceService::reconnect);
  // Observe the completed transition, including a child that finishes a slow stop.
  connect(&m_backend, &VoiceBackend::changed, this, &VoiceService::updateRetry, Qt::QueuedConnection);
  connect(&m_receiver, &VoiceReceiver::changed, this, &VoiceService::updateRetry, Qt::QueuedConnection);
}
void VoiceService::updateRetry()
{
  if (!m_running || !m_configured) {
    m_retry.stop();
    return;
  }
#ifdef Q_OS_WIN
  const bool healthy = m_receiver.running();
  const bool active = m_receiver.active();
#else
  const bool healthy = m_backend.ready();
  const bool active = m_backend.connecting();
#endif
  if (healthy) {
    m_retry.stop();
    m_retryDelay = 1000;
  } else if (!active && !m_retry.isActive()) {
#ifdef Q_OS_WIN
    if (!m_receiver.startupRetryAllowed())
      return;
#endif
    m_retry.start(m_retryDelay);
    m_retryDelay = std::min(m_retryDelay * 2, 15000);
  }
}
void VoiceService::reconnect()
{
  if (!m_running || !m_configured)
    return;
#ifdef Q_OS_WIN
  if (!m_receiver.active() && m_receiver.startupRetryAllowed())
    m_receiver.start(m_path);
#else
  if (!m_backend.ready() && !m_backend.connecting())
    m_backend.connectPeer();
#endif
  // Only reconnect the idle transport. A fresh key press owns the next capture.
  updateRetry();
}
void VoiceService::endpointChanged()
{
  if (!m_running || !m_preferences || !m_preferences->enabled) {
    m_routing.detach();
    return;
  }
  m_routing.endpoint(m_core.voiceEndpointName(), m_core.voiceControlSecret(), m_core.voiceProcessId());
}
void VoiceService::shutdown()
{
  m_running = false;
  m_configured = false;
  m_retry.stop();
  m_routing.detach();
  m_backend.close();
  m_receiver.stop();
}
void VoiceService::apply()
{
  if (!m_running)
    return;
  QVariantMap values;
  for (const auto &key : Preferences::defaults().toValues().keys())
    values.insert(key, Settings::value(key));
  const auto wanted = Preferences::fromValues(values);
  auto path = Settings::value(Settings::Voice::PeerFile).toString();
#ifdef Q_OS_WIN
  path = Settings::value(Settings::Voice::ReceiverFile).toString();
#endif
  if (path.isEmpty())
#ifdef Q_OS_WIN
    path =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/voice/receiver.json");
#else
    path = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/voice/peer.json");
#endif
  const auto modified = QFileInfo(path).lastModified();
  if (m_preferences && wanted == *m_preferences && path == m_path && modified == m_modified)
    return;
  m_configured = false;
  m_retry.stop();
  m_retryDelay = 1000;
  m_preferences = wanted;
  m_path = path;
  m_modified = modified;
  m_routing.configure(false, {}, {});
  m_routing.detach();
  m_backend.close();
  m_receiver.stop();
  if (!wanted.enabled)
    return;
#ifdef Q_OS_WIN
  m_configured = true;
  m_receiver.start(path);
  updateRetry();
  return;
#endif
#ifndef Q_OS_MACOS
  m_backend.unavailable(tr("此平台发送端尚未接入"));
  return;
#endif
  try {
    const auto peer = VoicePeer::load(path);
    if (peer.screen.isEmpty()) {
      m_backend.unavailable(tr("配对信息尚未绑定 Deskflow 电脑"));
      return;
    }
    m_backend.configure(peer);
    m_backend.setInputUid(wanted.inputUid);
    m_routing.configure(true, peer.screen, wanted.shortcut);
    endpointChanged();
    m_configured = true;
    m_backend.connectPeer();
  } catch (const std::exception &) {
    m_backend.unavailable(tr("请先准备 Windows 语音配对信息"));
  }
}
} // namespace deskflow::gui::voice
