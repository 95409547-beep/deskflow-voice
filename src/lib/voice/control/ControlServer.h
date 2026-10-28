/*
 * SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include "Protocol.h"
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QTimer>

namespace deskflow::voice::control {
class ControlServer : public QObject
{
  Q_OBJECT
public:
  ControlServer(QObject *parent, QByteArray secret, QString name, qint64 pid, bool installCore = false);
  ~ControlServer() override;
  bool listen();
  static ControlServer *current();
  void context(ScreenContext context);
  bool down(HoldKey key, std::uint32_t button, bool modifiersAllowed);
  bool up(std::uint32_t button);
  bool repeat(std::uint32_t button) const;
  HoldKey suppressedModifier() const;

private:
  void accept();
  void read();
  void message(const QJsonObject &value);
  void send(const QJsonObject &value);
  void dispatch(std::vector<KeyGateEvent> events);
  void drop();
  static std::int64_t now();
  QLocalServer m_server;
  QPointer<QLocalSocket> m_client;
  QTimer m_handshake, m_watchdog;
  Frames m_frames;
  KeyGate m_gate;
  QByteArray m_secret;
  QString m_name, m_epoch, m_lease, m_nonce;
  qint64 m_pid;
  bool m_authenticated = false;
};
} // namespace deskflow::voice::control
