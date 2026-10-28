/*
 * SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "VoiceRouting.h"
#include "VoiceBackend.h"
#include <QUuid>

namespace deskflow::gui::voice {
using namespace deskflow::voice::control;
namespace {
quint64 number(const QJsonObject &object, const QString &name)
{
  const auto text = object.value(name).toString();
  bool valid = false;
  const auto value = text.toULongLong(&valid);
  if (!valid || QString::number(value) != text)
    throw std::runtime_error("invalid control counter");
  return value;
}
} // namespace
VoiceRouting::VoiceRouting(VoiceBackend &backend, QObject *parent) : QObject(parent), m_backend(backend)
{
  m_backend.requireScreenRouting(true);
  m_socket.setReadBufferSize(Frames::maximum * 2 + 8);
  m_ping.setInterval(1000);
  m_retry.setInterval(1000);
  m_retry.setSingleShot(true);
  m_deadline.setSingleShot(true);
  connect(&m_socket, &QLocalSocket::connected, this, [this] {
    m_nonce = QUuid::createUuid().toString(QUuid::WithoutBraces);
    send(
        {{QStringLiteral("type"), QStringLiteral("hello")},
         {QStringLiteral("protocol"), 1},
         {QStringLiteral("nonce"), m_nonce}}
    );
    m_deadline.start(3000);
  });
  connect(&m_socket, &QLocalSocket::readyRead, this, &VoiceRouting::read);
  connect(&m_socket, &QLocalSocket::disconnected, this, &VoiceRouting::lost);
  connect(&m_socket, &QLocalSocket::errorOccurred, this, &VoiceRouting::lost);
  connect(&m_deadline, &QTimer::timeout, this, &VoiceRouting::lost);
  connect(&m_retry, &QTimer::timeout, this, &VoiceRouting::connectEndpoint);
  connect(&m_ping, &QTimer::timeout, this, [this] {
    if (++m_missed > 3) {
      lost();
      return;
    }
    send({{QStringLiteral("type"), QStringLiteral("renew")}});
  });
  connect(&backend, &VoiceBackend::changed, this, &VoiceRouting::updateConfiguration);
}
VoiceRouting::~VoiceRouting()
{
  detach();
  disconnect(&m_socket, nullptr, this, nullptr);
  disconnect(&m_backend, nullptr, this, nullptr);
}
void VoiceRouting::endpoint(const QString &name, const QByteArray &secret, qint64 pid)
{
  if (name == m_name && secret == m_secret && pid == m_pid)
    return;
  detach();
  m_name = name;
  m_secret = secret;
  m_pid = pid;
  if (!name.isEmpty() && secret.size() == 32 && pid > 0)
    connectEndpoint();
}
void VoiceRouting::configure(bool enabled, const QString &screen, const QString &shortcut)
{
  if (m_enabled == enabled && m_screen == screen && m_shortcut == shortcut)
    return;
  m_backend.controlRoute({}, 0, false);
  m_backend.release();
  m_enabled = enabled;
  m_screen = screen;
  m_shortcut = shortcut;
  m_lastConfiguration = {};
  updateConfiguration();
}
void VoiceRouting::detach()
{
  m_closing = true;
  m_retry.stop();
  m_deadline.stop();
  m_ping.stop();
  m_authenticated = m_verified = false;
  m_gateEnabled = false;
  m_backend.controlAvailable(false);
  m_socket.abort();
  m_frames.clear();
  m_epoch.clear();
  m_lease.clear();
  m_name.clear();
  m_secret.clear();
  m_pid = 0;
  m_serial = m_generation = m_hold = 0;
  m_waitRelease = false;
  m_lastConfiguration = {};
  m_backend.controlRoute({}, 0, false);
  m_backend.release();
  m_closing = false;
  Q_EMIT changed();
}
void VoiceRouting::connectEndpoint()
{
  if (m_name.isEmpty() || m_secret.size() != 32 || m_pid <= 0)
    return;
  m_socket.connectToServer(m_name);
}
void VoiceRouting::lost()
{
  if (m_closing)
    return;
  const auto name = m_name;
  const auto secret = m_secret;
  const auto pid = m_pid;
  detach();
  m_name = name;
  m_secret = secret;
  m_pid = pid;
  if (!name.isEmpty())
    m_retry.start();
}
void VoiceRouting::send(QJsonObject value)
{
  if (m_socket.state() != QLocalSocket::ConnectedState)
    return;
  if (m_verified) {
    value.insert(QStringLiteral("epoch"), m_epoch);
    value.insert(QStringLiteral("lease"), m_lease);
  }
  const auto bytes = Frames::encode(value);
  if (m_socket.bytesToWrite() + bytes.size() > 65536 || m_socket.write(bytes) != bytes.size())
    lost();
}
void VoiceRouting::read()
{
  try {
    while (m_socket.bytesAvailable()) {
      for (const auto &value : m_frames.append(m_socket.read(2048))) {
        message(value);
        if (m_socket.state() != QLocalSocket::ConnectedState)
          return;
      }
    }
  } catch (const std::exception &) {
    lost();
  }
}
void VoiceRouting::message(const QJsonObject &value)
{
  const auto type = value.value(QStringLiteral("type")).toString();
  if (!m_verified && type == QStringLiteral("hello")) {
    const auto epoch = value.value(QStringLiteral("epoch")).toString(),
               lease = value.value(QStringLiteral("lease")).toString();
    if (value.value(QStringLiteral("protocol")).toInt() != 1 ||
        value.value(QStringLiteral("pid")).toInteger() != m_pid || !uuid(epoch) || !uuid(lease) ||
        !equalProof(
            value.value(QStringLiteral("proof")).toString().toLatin1(),
            proof(m_secret, QStringLiteral("server"), m_nonce, epoch, lease, m_pid)
        )) {
      lost();
      return;
    }
    m_epoch = epoch;
    m_lease = lease;
    m_verified = true;
    send(
        {{QStringLiteral("type"), QStringLiteral("authenticate")},
         {QStringLiteral("proof"),
          QString::fromLatin1(proof(m_secret, QStringLiteral("client"), m_nonce, epoch, lease, m_pid))}}
    );
    return;
  }
  if (!m_verified || value.value(QStringLiteral("epoch")).toString() != m_epoch ||
      value.value(QStringLiteral("lease")).toString() != m_lease) {
    lost();
    return;
  }
  if (!m_authenticated && type == QStringLiteral("authenticated")) {
    m_authenticated = true;
    m_deadline.stop();
    m_missed = 0;
    m_ping.start();
    m_lastConfiguration = {};
    m_backend.controlAvailable(true);
    updateConfiguration();
    Q_EMIT changed();
    return;
  }
  if (!m_authenticated) {
    lost();
    return;
  }
  if (type == QStringLiteral("pong")) {
    m_missed = 0;
    if (!value.value(QStringLiteral("renewed")).toBool() && m_enabled && m_backend.ready()) {
      m_lastConfiguration = {};
      updateConfiguration();
    }
    return;
  }
  const auto serial = number(value, QStringLiteral("serial"));
  if (!serial || serial <= m_serial)
    return;
  m_serial = serial;
  const auto generation = number(value, QStringLiteral("generation")), hold = number(value, QStringLiteral("hold"));
  if (type == QStringLiteral("state")) {
    m_generation = generation;
    m_gateEnabled = value.value(QStringLiteral("enabled")).toBool();
    const bool held = value.value(QStringLiteral("held")).toBool();
    if (!held) {
      m_backend.release();
      m_hold = 0;
      m_waitRelease = false;
    } else if (!m_hold)
      m_waitRelease = true;
    const bool eligible = m_enabled && value.value(QStringLiteral("enabled")).toBool() &&
                          value.value(QStringLiteral("remote")).toBool() &&
                          value.value(QStringLiteral("eligible")).toBool();
    m_backend.controlRoute(value.value(QStringLiteral("screen")).toString(), generation, eligible);
    Q_EMIT changed();
  } else if (type == QStringLiteral("down")) {
    if (!m_enabled || m_waitRelease || m_hold || generation != m_generation || !hold ||
        !value.value(QStringLiteral("enabled")).toBool() || !value.value(QStringLiteral("remote")).toBool() ||
        !value.value(QStringLiteral("eligible")).toBool() ||
        value.value(QStringLiteral("screen")).toString() != m_screen)
      return;
    m_hold = hold;
    m_backend.press();
  } else if (type == QStringLiteral("up")) {
    if (hold == m_hold || m_waitRelease) {
      m_backend.release();
      m_hold = 0;
      m_waitRelease = false;
    }
  } else if (type == QStringLiteral("cancel")) {
    if (hold == m_hold)
      m_backend.controlRoute({}, 0, false);
  } else
    lost();
}
void VoiceRouting::updateConfiguration()
{
  if (!m_authenticated)
    return;
  const QJsonObject config{
      {QStringLiteral("type"), QStringLiteral("configure")},
      {QStringLiteral("enabled"), m_enabled && m_backend.ready()},
      {QStringLiteral("screen"), m_screen},
      {QStringLiteral("shortcut"), m_shortcut}
  };
  if (config == m_lastConfiguration)
    return;
  m_lastConfiguration = config;
  send(config);
}
} // namespace deskflow::gui::voice
