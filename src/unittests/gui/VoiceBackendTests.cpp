/*
 * SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
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
class VoiceBackendTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void framingIsIncrementalAndBounded()
  {
    VoiceWire wire;
    const QJsonObject expected{{QStringLiteral("type"), QStringLiteral("ping")}};
    const auto encoded = VoiceWire::encode(expected);
    QVERIFY(wire.append(encoded.left(3)).isEmpty());
    const auto decoded = wire.append(encoded.mid(3) + encoded);
    QCOMPARE(decoded.size(), 2);
    QCOMPARE(decoded.first(), expected);
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, wire.append(QByteArray(4, '\0')));
    wire.clear();
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, wire.append(QByteArray::fromHex("00010001")));
    wire.clear();
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, wire.append(QByteArray::fromHex("000000025b5d")));
  }
  void repeatedRoundsWaitForAckAndStop()
  {
    LoopbackReceiver receiver;
    auto *capture = new FakeCapture;
    VoiceBackend backend(nullptr, capture);
    backend.configure(receiver.peer());
    backend.connectPeer();
    QTRY_VERIFY_WITH_TIMEOUT(backend.ready(), 7000);
    for (int index = 0; index < 4; ++index) {
      const auto previousBytes = receiver.bytes;
      backend.press();
      backend.press(); // duplicate DOWN must not replace sequence
      capture->frame();
      QCOMPARE(receiver.bytes, previousBytes); // no forwarding before native-start ack
      QTRY_COMPARE_WITH_TIMEOUT(receiver.bytes, previousBytes + 1920, 3000);
      backend.release();
      QVERIFY(!capture->active);
      QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
      QVERIFY(backend.ready());
    }
    QCOMPARE(receiver.begins, 4);
    QCOMPARE(receiver.ends, 4);
    QCOMPARE(
        receiver.sequences,
        (QStringList{QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"), QStringLiteral("4")})
    );
    QCOMPARE(capture->starts, 4);
    QCOMPARE(capture->stops, 4);
  }
  void earlyReleaseDoesNotForwardPrerollOrResumeOnStart()
  {
    LoopbackReceiver receiver;
    receiver.autoStart = receiver.autoEnd = false;
    auto *capture = new FakeCapture;
    VoiceBackend backend(nullptr, capture);
    backend.configure(receiver.peer());
    backend.connectPeer();
    QTRY_VERIFY_WITH_TIMEOUT(backend.ready(), 7000);
    backend.press();
    capture->frame();
    backend.release();
    QTRY_COMPARE_WITH_TIMEOUT(receiver.ends, 1, 3000);
    receiver.started();
    QTest::qWait(30);
    QVERIFY(!capture->active);
    QCOMPARE(receiver.bytes, 0);
    receiver.ended();
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
    QVERIFY(backend.ready());
  }
  void receiverStopRequiresNewPhysicalPress()
  {
    LoopbackReceiver receiver;
    auto *capture = new FakeCapture;
    VoiceBackend backend(nullptr, capture);
    backend.configure(receiver.peer());
    backend.connectPeer();
    QTRY_VERIFY_WITH_TIMEOUT(backend.ready(), 7000);
    backend.press();
    QTRY_COMPARE_WITH_TIMEOUT(receiver.begins, 1, 3000);
    receiver.ended();
    QTRY_VERIFY_WITH_TIMEOUT(!capture->active, 3000);
    backend.press();
    QCOMPARE(capture->starts, 1);
    backend.release();
    backend.press();
    QTRY_COMPARE_WITH_TIMEOUT(receiver.begins, 2, 3000);
    backend.release();
  }
  void identityMismatchNeverSendsToken()
  {
    LoopbackReceiver receiver;
    auto *capture = new FakeCapture;
    auto peer = receiver.peer();
    peer.certificateSha256.fill('x');
    VoiceBackend backend(nullptr, capture);
    backend.configure(peer);
    backend.connectPeer();
    QTRY_VERIFY_WITH_TIMEOUT(backend.status().contains(QStringLiteral("身份不匹配")), 7000);
    QVERIFY(!backend.ready());
    QCOMPARE(receiver.hellos, 0);
    QCOMPARE(capture->starts, 0);
  }
  void crossedEndAndLateStartDoNotBreakNextRound()
  {
    LoopbackReceiver receiver;
    auto *capture = new FakeCapture;
    VoiceBackend backend(nullptr, capture);
    backend.configure(receiver.peer());
    backend.connectPeer();
    QTRY_VERIFY_WITH_TIMEOUT(backend.ready(), 7000);
    backend.press();
    QTRY_COMPARE_WITH_TIMEOUT(receiver.begins, 1, 3000);
    receiver.ended(); // receiver cancellation crosses the local physical UP
    backend.release();
    QTRY_COMPARE_WITH_TIMEOUT(receiver.ends, 1, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
    receiver.started(); // a stale confirmation may not restart or disconnect
    QTest::qWait(30);
    QVERIFY(backend.ready());
    QVERIFY(!capture->active);
    backend.press();
    QTRY_COMPARE_WITH_TIMEOUT(receiver.begins, 2, 3000);
    backend.release();
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
    QVERIFY(backend.ready());
  }
  void oldEpochStopsCapture()
  {
    LoopbackReceiver receiver;
    auto *capture = new FakeCapture;
    VoiceBackend backend(nullptr, capture);
    backend.configure(receiver.peer());
    backend.connectPeer();
    QTRY_VERIFY_WITH_TIMEOUT(backend.ready(), 7000);
    backend.press();
    QTRY_COMPARE_WITH_TIMEOUT(receiver.begins, 1, 3000);
    receiver.write(
        {{QStringLiteral("type"), QStringLiteral("pong")}, {QStringLiteral("epoch"), QStringLiteral("old-epoch")}}
    );
    QTRY_VERIFY_WITH_TIMEOUT(!backend.ready(), 3000);
    QVERIFY(!capture->active);
  }
  void permissionCompletionDoesNotStartCapture()
  {
    LoopbackReceiver receiver;
    auto *capture = new FakeCapture;
    capture->allowed = false;
    VoiceBackend backend(nullptr, capture);
    backend.configure(receiver.peer());
    backend.connectPeer();
    QTRY_VERIFY_WITH_TIMEOUT(backend.ready(), 7000);
    backend.press();
    backend.release();
    QVERIFY(capture->permission);
    capture->allowed = true;
    capture->permission(true);
    QCOMPARE(capture->starts, 0);
    QCOMPARE(receiver.begins, 0);
    backend.press();
    QTRY_COMPARE_WITH_TIMEOUT(receiver.begins, 1, 3000);
    backend.release();
  }
};
QTEST_MAIN(VoiceBackendTests)
#include "VoiceBackendTests.moc"
