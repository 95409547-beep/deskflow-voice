/*
 * SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include "VoiceWire.h"
#include "voice/Session.h"
#include <QObject>
#include <QSet>
#include <QSslSocket>
#include <QTimer>
#include <memory>

namespace deskflow::gui::voice {
class AudioCapture;
class VoiceBackend : public QObject
{
  Q_OBJECT
public:
  explicit VoiceBackend(QObject *parent = nullptr, AudioCapture *capture = nullptr);
  ~VoiceBackend() override;
  void configure(const VoicePeer &peer);
  void setInputUid(const QString &uid);
  void connectPeer();
  void close();
  void press();
  void release();
  bool ready() const
  {
    return m_ready;
  }
  bool busy() const;
  QString status() const
  {
    return m_status;
  }
Q_SIGNALS:
  void changed();
  void roundEnded(quint64 pcmBytes, const QString &reason);

private:
  void read();
  void message(const QJsonObject &object);
  void send(QJsonObject object);
  void effects(const std::vector<deskflow::voice::Effect> &effects);
  void audio(const QByteArray &bytes);
  void fail(const QString &error);
  void setStatus(const QString &status);
  QJsonObject roundMessage(const QString &type) const;
  QSslSocket m_socket;
  QTimer m_deadline, m_heartbeat;
  VoiceWire m_wire;
  VoicePeer m_peer;
  AudioCapture *m_capture;
  deskflow::voice::Session m_session{deskflow::voice::Platform::MacOS};
  QString m_epoch, m_round, m_uid, m_status;
  QSet<QString> m_completed;
  QStringList m_completedOrder;
  quint64 m_sequence = 0;
  QByteArray m_preroll;
  bool m_ready = false, m_closing = false, m_permissionPending = false;
  int m_silentHeartbeats = 0;
};
} // namespace deskflow::gui::voice
