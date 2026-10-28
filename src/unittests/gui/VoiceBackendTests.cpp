/*
 * SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "VoiceTestSupport.h"

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
    QVERIFY(backend.status().contains(QStringLiteral("请先松开")));
    backend.press();
    QCOMPARE(capture->starts, 1);
    backend.release();
    QCOMPARE(backend.status(), QStringLiteral("已结束，可以继续下一句"));
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
