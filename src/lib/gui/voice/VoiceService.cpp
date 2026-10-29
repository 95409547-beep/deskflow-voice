/*
 * SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "VoiceService.h"
#include "common/Settings.h"
#include "gui/core/CoreProcess.h"
#include <QFileInfo>
#include <QStandardPaths>

namespace deskflow::gui::voice {
VoiceService::VoiceService(CoreProcess &core, QObject *parent, AudioCapture *capture)
    : QObject(parent),
      m_core(core),
      m_backend(this, capture),
      m_routing(m_backend, this)
{
  connect(&core, &CoreProcess::voiceEndpointChanged, this, &VoiceService::endpointChanged);
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
  m_routing.detach();
  m_backend.close();
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
  if (path.isEmpty())
    path = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/voice/peer.json");
  const auto modified = QFileInfo(path).lastModified();
  if (m_preferences && wanted == *m_preferences && path == m_path && modified == m_modified)
    return;
  m_preferences = wanted;
  m_path = path;
  m_modified = modified;
  m_routing.configure(false, {}, {});
  m_routing.detach();
  m_backend.close();
  if (!wanted.enabled)
    return;
#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN)
  m_backend.unavailable(tr("此平台发送端尚未接入"));
  return;
#endif
#ifdef Q_OS_WIN
  if (wanted.role == QStringLiteral("receive")) {
    m_backend.unavailable(tr("接收语音由 Windows 语音组件提供"));
    return;
  }
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
    m_backend.connectPeer();
  } catch (const std::exception &) {
    m_backend.unavailable(tr("请先准备 Windows 语音配对信息"));
  }
}
} // namespace deskflow::gui::voice
