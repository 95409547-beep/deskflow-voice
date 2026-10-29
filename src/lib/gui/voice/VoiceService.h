/*
 * SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include "VoiceBackend.h"
#include "VoicePreferences.h"
#include "VoiceReceiver.h"
#include "VoiceRouting.h"
#include <QDateTime>
#include <optional>

namespace deskflow::gui {
class CoreProcess;
namespace voice {
class VoiceService : public QObject
{
  Q_OBJECT
public:
  explicit VoiceService(CoreProcess &core, QObject *parent = nullptr, AudioCapture *capture = nullptr);
  VoiceBackend *backend()
  {
    return &m_backend;
  }
  VoiceReceiver *receiver()
  {
    return &m_receiver;
  }
  void apply();
  void shutdown();
  void retry()
  {
    m_preferences.reset();
    apply();
  }

private:
  void endpointChanged();
  CoreProcess &m_core;
  VoiceBackend m_backend;
  VoiceRouting m_routing;
  VoiceReceiver m_receiver;
  std::optional<Preferences> m_preferences;
  QString m_path;
  QDateTime m_modified;
  bool m_running = true;
};
} // namespace voice
} // namespace deskflow::gui
