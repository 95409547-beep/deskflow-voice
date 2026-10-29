/*
 * SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include <QObject>
#include <QProcess>

namespace deskflow::gui::voice {
// Owns only its child receiver. It never attaches to or kills another receiver.
class VoiceReceiver : public QObject
{
  Q_OBJECT
public:
  explicit VoiceReceiver(QObject *parent = nullptr, QString executable = {});
  ~VoiceReceiver() override;
  void start(const QString &config);
  bool stop();
  bool running() const;
  bool connected() const
  {
    return m_connected;
  }
  bool recording() const
  {
    return m_recording;
  }
  QString status() const
  {
    return m_status;
  }
Q_SIGNALS:
  void changed();

private:
  void read();
  void setStatus(const QString &status);
  QProcess *m_process = nullptr;
  QString m_executable, m_config, m_status;
  QByteArray m_lines;
  bool m_connected = false, m_recording = false, m_stopping = false;
};
} // namespace deskflow::gui::voice
