/*
 * SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "ControlServer.h"
#include <QFile>
#include <QUuid>
#include <chrono>
#include <utility>

namespace deskflow::voice::control {
static ControlServer *coreServer = nullptr;
ControlServer *ControlServer::current()
{
  return coreServer;
}
std::int64_t ControlServer::now()
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
ControlServer::ControlServer(QObject *parent, QByteArray secret, QString name, qint64 pid, bool installCore)
    : QObject(parent),
      m_secret(std::move(secret)),
      m_name(std::move(name)),
      m_epoch(QUuid::createUuid().toString(QUuid::WithoutBraces)),
      m_pid(pid)
{
  if (installCore) {
    Q_ASSERT(!coreServer);
    coreServer = this;
  }
  m_server.setSocketOptions(QLocalServer::UserAccessOption);
  m_server.setMaxPendingConnections(1);
  m_handshake.setSingleShot(true);
  m_watchdog.setInterval(250);
  connect(&m_server, &QLocalServer::newConnection, this, &ControlServer::accept);
  connect(&m_handshake, &QTimer::timeout, this, &ControlServer::drop);
  connect(&m_watchdog, &QTimer::timeout, this, [this] { dispatch(m_gate.tick(now())); });
}
ControlServer::~ControlServer()
{
  if (coreServer == this)
    coreServer = nullptr;
  m_watchdog.stop();
  drop();
  m_server.close();
}
bool ControlServer::listen()
{
  if (m_secret.size() != 32 || m_name.isEmpty())
    return false;
  // A fresh child PID has no live namesake. Never remove another live endpoint.
  if (!m_server.listen(m_name))
    return false;
#ifdef Q_OS_UNIX
  if (!QFile::setPermissions(m_server.fullServerName(), QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
    m_server.close();
    return false;
  }
#endif
  m_watchdog.start();
  return true;
}
void ControlServer::accept()
{
  while (m_server.hasPendingConnections()) {
    auto *candidate = m_server.nextPendingConnection();
    if (m_client) {
      candidate->abort();
      candidate->deleteLater();
      continue;
    }
    m_client = candidate;
    m_lease = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_authenticated = false;
    m_nonce.clear();
    m_frames.clear();
    candidate->setReadBufferSize(Frames::maximum * 2 + 8);
    connect(candidate, &QLocalSocket::readyRead, this, &ControlServer::read);
    connect(candidate, &QLocalSocket::disconnected, this, [this, candidate] {
      if (m_client == candidate)
        drop();
    });
    connect(candidate, &QLocalSocket::errorOccurred, this, [this, candidate] {
      if (m_client == candidate)
        drop();
    });
    m_handshake.start(3000);
  }
}
void ControlServer::drop()
{
  m_authenticated = false;
  m_handshake.stop();
  m_gate.disarm();
  if (m_client) {
    auto *client = m_client.data();
    m_client = nullptr;
    disconnect(client, nullptr, this, nullptr);
    client->abort();
    client->deleteLater();
  }
  m_frames.clear();
  m_lease.clear();
  m_nonce.clear();
}
void ControlServer::send(const QJsonObject &value)
{
  if (!m_client)
    return;
  const auto frame = Frames::encode(value);
  if (m_client->bytesToWrite() + frame.size() > 65536 || m_client->write(frame) != frame.size())
    drop();
}
void ControlServer::read()
{
  try {
    while (m_client && m_client->bytesAvailable()) {
      for (const auto &value : m_frames.append(m_client->read(2048))) {
        message(value);
        if (!m_client)
          return;
      }
    }
  } catch (const std::exception &) {
    drop();
  }
}
void ControlServer::message(const QJsonObject &value)
{
  const auto type = value.value(QStringLiteral("type")).toString();
  if (type == QStringLiteral("hello") && m_nonce.isEmpty() && !m_authenticated) {
    m_nonce = value.value(QStringLiteral("nonce")).toString();
    if (value.value(QStringLiteral("protocol")).toInt() != 1 || !uuid(m_nonce)) {
      drop();
      return;
    }
    send(
        {{QStringLiteral("type"), QStringLiteral("hello")},
         {QStringLiteral("protocol"), 1},
         {QStringLiteral("epoch"), m_epoch},
         {QStringLiteral("lease"), m_lease},
         {QStringLiteral("pid"), m_pid},
         {QStringLiteral("proof"),
          QString::fromLatin1(proof(m_secret, QStringLiteral("server"), m_nonce, m_epoch, m_lease, m_pid))}}
    );
    return;
  }
  if (type == QStringLiteral("authenticate") && !m_authenticated && !m_nonce.isEmpty()) {
    if (!equalProof(
            value.value(QStringLiteral("proof")).toString().toLatin1(),
            proof(m_secret, QStringLiteral("client"), m_nonce, m_epoch, m_lease, m_pid)
        )) {
      drop();
      return;
    }
    m_authenticated = true;
    m_handshake.stop();
    send(
        {{QStringLiteral("type"), QStringLiteral("authenticated")},
         {QStringLiteral("epoch"), m_epoch},
         {QStringLiteral("lease"), m_lease}}
    );
    dispatch(m_gate.configure(m_lease.toStdString(), {}, HoldKey::Other, false, now()));
    dispatch({m_gate.snapshot()});
    return;
  }
  if (!m_authenticated || value.value(QStringLiteral("epoch")).toString() != m_epoch ||
      value.value(QStringLiteral("lease")).toString() != m_lease) {
    drop();
    return;
  }
  if (type == QStringLiteral("configure")) {
    const auto target = value.value(QStringLiteral("screen")).toString();
    if (target.size() > 255 || target.contains(QChar('\0'))) {
      drop();
      return;
    }
    dispatch(m_gate.configure(
        m_lease.toStdString(), target.toStdString(), parseKey(value.value(QStringLiteral("shortcut")).toString()),
        value.value(QStringLiteral("enabled")).toBool(), now()
    ));
    dispatch({m_gate.snapshot()});
  } else if (type == QStringLiteral("renew")) {
    const auto renewed = m_gate.renew(m_lease.toStdString(), now());
    send(
        {{QStringLiteral("type"), QStringLiteral("pong")},
         {QStringLiteral("epoch"), m_epoch},
         {QStringLiteral("lease"), m_lease},
         {QStringLiteral("renewed"), renewed}}
    );
  } else
    drop();
}
void ControlServer::dispatch(std::vector<KeyGateEvent> events)
{
  if (events.empty())
    return;
  QMetaObject::invokeMethod(
      this,
      [this, events = std::move(events)] {
        if (!m_client || !m_authenticated)
          return;
        for (const auto &event : events) {
          if (!m_client || event.lease != m_lease.toStdString())
            continue;
          const QString type = event.kind == KeyEventKind::Down     ? QStringLiteral("down")
                               : event.kind == KeyEventKind::Up     ? QStringLiteral("up")
                               : event.kind == KeyEventKind::Cancel ? QStringLiteral("cancel")
                                                                    : QStringLiteral("state");
          send(
              {{QStringLiteral("type"), type},
               {QStringLiteral("epoch"), m_epoch},
               {QStringLiteral("lease"), m_lease},
               {QStringLiteral("serial"), QString::number(event.serial)},
               {QStringLiteral("generation"), QString::number(event.generation)},
               {QStringLiteral("hold"), QString::number(event.hold)},
               {QStringLiteral("screen"), QString::fromStdString(event.screen.name)},
               {QStringLiteral("remote"), event.screen.remote},
               {QStringLiteral("eligible"), event.screen.eligible},
               {QStringLiteral("held"), event.held},
               {QStringLiteral("enabled"), event.enabled}}
          );
        }
      },
      Qt::QueuedConnection
  );
}
void ControlServer::context(ScreenContext context)
{
  dispatch(m_gate.context(std::move(context)));
}
bool ControlServer::down(HoldKey key, std::uint32_t button, bool modifiersAllowed)
{
  auto decision = m_gate.down(key, button, modifiersAllowed, now());
  dispatch(std::move(decision.events));
  return decision.consume;
}
bool ControlServer::up(std::uint32_t button)
{
  auto decision = m_gate.up(button, now());
  dispatch(std::move(decision.events));
  return decision.consume;
}
bool ControlServer::repeat(std::uint32_t button) const
{
  return m_gate.repeat(button);
}
HoldKey ControlServer::suppressedModifier() const
{
  return m_gate.suppressedModifier();
}
} // namespace deskflow::voice::control
