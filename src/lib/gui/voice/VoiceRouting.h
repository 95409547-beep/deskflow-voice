/*
 * SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include "voice/control/Protocol.h"
#include <QLocalSocket>
#include <QTimer>

namespace deskflow::gui::voice {
class VoiceBackend;
class VoiceRouting : public QObject
{
  Q_OBJECT
public:
  explicit VoiceRouting(VoiceBackend &backend, QObject *parent = nullptr);
  ~VoiceRouting() override;
  void endpoint(const QString &name, const QByteArray &secret, qint64 pid);
  void configure(bool enabled, const QString &screen, const QString &shortcut);
  void detach();
  bool connected() const
  {
    return m_authenticated;
  }
  bool armed() const
  {
    return m_authenticated && m_gateEnabled;
  }
Q_SIGNALS:
  void changed();

private:
  void connectEndpoint();
  void lost();
  void read();
  void message(const QJsonObject &value);
  void send(QJsonObject value);
  void updateConfiguration();
  VoiceBackend &m_backend;
  QLocalSocket m_socket;
  deskflow::voice::control::Frames m_frames;
  QTimer m_ping, m_deadline, m_retry;
  QString m_name, m_epoch, m_lease, m_nonce, m_screen, m_shortcut;
  QByteArray m_secret;
  qint64 m_pid = 0;
  quint64 m_serial = 0, m_generation = 0, m_hold = 0;
  bool m_authenticated = false, m_verified = false, m_closing = false, m_enabled = false, m_waitRelease = false;
  int m_missed = 0;
  bool m_gateEnabled = false;
  QJsonObject m_lastConfiguration;
};
} // namespace deskflow::gui::voice
