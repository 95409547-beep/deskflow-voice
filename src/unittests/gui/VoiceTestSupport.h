/*
 * SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include "VoiceTlsFixture.h"
#include "gui/voice/AudioCapture.h"
#include "gui/voice/VoiceBackend.h"
#include <QJsonDocument>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslServer>
#include <QTest>
#include <QUuid>

using namespace deskflow::gui::voice;
// Unsupported hosts still exercise the portable sender with fake audio.
inline constexpr auto testSenderPlatform = VoiceBackend::senderPlatform() == deskflow::voice::Platform::Unsupported
                                               ? deskflow::voice::Platform::MacOS
                                               : VoiceBackend::senderPlatform();
class FakeCapture : public AudioCapture
{
public:
  bool allowed = true, active = false;
  int starts = 0, stops = 0;
  std::function<void(bool)> permission;
  bool permitted() const override
  {
    return allowed;
  }
  void requestPermission(std::function<void(bool)> done) override
  {
    permission = std::move(done);
  }
  bool start(const QString &, QString &) override
  {
    active = true;
    ++starts;
    return true;
  }
  void stop() override
  {
    if (active)
      ++stops;
    active = false;
  }
  void frame()
  {
    if (active)
      Q_EMIT pcm(QByteArray(1920, '\0'));
  }
};
class LoopbackReceiver : public QObject
{
public:
  QSslServer server;
  QSslSocket *client = nullptr;
  VoiceWire wire;
  QString epoch = QUuid::createUuid().toString(QUuid::WithoutBraces), round;
  int begins = 0, ends = 0, hellos = 0, bytes = 0;
  bool autoStart = true, autoEnd = true;
  QStringList sequences;
  QSslCertificate certificate{QByteArray(voice_test::certificate)};
  LoopbackReceiver()
  {
    auto config = QSslConfiguration::defaultConfiguration();
    config.setLocalCertificate(certificate);
    config.setPrivateKey(QSslKey(QByteArray(voice_test::key), QSsl::Rsa));
    config.setPeerVerifyMode(QSslSocket::VerifyNone);
    server.setSslConfiguration(config);
    connect(&server, &QSslServer::pendingConnectionAvailable, this, [this] {
      client = qobject_cast<QSslSocket *>(server.nextPendingConnection());
      connect(client, &QSslSocket::readyRead, this, [this] {
        for (const auto &message : wire.append(client->readAll())) {
          const auto type = message.value(QStringLiteral("type")).toString();
          if (type == QStringLiteral("hello")) {
            ++hellos;
            write(
                {{QStringLiteral("type"), QStringLiteral("hello")},
                 {QStringLiteral("protocol"), 2},
                 {QStringLiteral("receiver"), QStringLiteral("windows")},
                 {QStringLiteral("ready"), true},
                 {QStringLiteral("sample_rate"), 48000},
                 {QStringLiteral("channels"), 1},
                 {QStringLiteral("format"), QStringLiteral("s16le")},
                 {QStringLiteral("epoch"), epoch}}
            );
          } else if (type == QStringLiteral("begin")) {
            ++begins;
            round = message.value(QStringLiteral("session")).toString();
            sequences << message.value(QStringLiteral("sequence")).toString();
            if (autoStart)
              started();
          } else if (type == QStringLiteral("audio")) {
            bytes += QByteArray::fromBase64(message.value(QStringLiteral("pcm")).toString().toLatin1()).size();
          } else if (type == QStringLiteral("end")) {
            ++ends;
            if (autoEnd)
              ended();
          } else if (type == QStringLiteral("ping"))
            write({{QStringLiteral("type"), QStringLiteral("pong")}, {QStringLiteral("epoch"), epoch}});
        }
      });
    });
    if (!server.listen(QHostAddress::LocalHost))
      throw std::runtime_error("loopback listen failed");
  }
  VoicePeer peer() const
  {
    return {
        QStringLiteral("127.0.0.1"), server.serverPort(), certificate.digest(QCryptographicHash::Sha256),
        QString(64, QLatin1Char('a'))
    };
  }
  void write(const QJsonObject &value)
  {
    client->write(VoiceWire::encode(value));
  }
  void started()
  {
    write(
        {{QStringLiteral("type"), QStringLiteral("started")},
         {QStringLiteral("epoch"), epoch},
         {QStringLiteral("session"), round}}
    );
  }
  void ended()
  {
    write(
        {{QStringLiteral("type"), QStringLiteral("ended")},
         {QStringLiteral("epoch"), epoch},
         {QStringLiteral("session"), round},
         {QStringLiteral("ready"), true},
         {QStringLiteral("audio_bytes"), bytes},
         {QStringLiteral("reason"), QStringLiteral("fixture-end")}}
    );
  }
};
