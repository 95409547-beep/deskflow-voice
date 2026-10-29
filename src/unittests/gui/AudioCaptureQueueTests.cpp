/*
 * SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "VoiceTestSupport.h"
#include "gui/voice/AudioCaptureQueue.h"
#include <QSignalSpy>
#include <thread>

class AudioCaptureQueueTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void packetBoundariesAndSilencePreserveWireFormat()
  {
    FakeCapture owner;
    AudioCaptureQueue queue(owner);
    QSignalSpy frames(&owner, &AudioCapture::pcm);
    const auto run = queue.begin();
    const QByteArray first(960, 'a'), second(960, 'b');
    QVERIFY(queue.append(run, first.constData(), 960));
    QCoreApplication::processEvents();
    QCOMPARE(frames.size(), 0);
    QVERIFY(queue.append(run, second.constData(), 960));
    QVERIFY(queue.append(run, nullptr, 3840, true));
    QTRY_COMPARE(frames.size(), 3);
    QCOMPARE(frames.at(0).at(0).toByteArray(), first + second);
    QCOMPARE(frames.at(1).at(0).toByteArray(), QByteArray(1920, '\0'));
    QCOMPARE(frames.at(2).at(0).toByteArray(), QByteArray(1920, '\0'));
  }
  void releaseDropsQueuedAudioPartialAndLateWorkerFailure()
  {
    FakeCapture owner;
    AudioCaptureQueue queue(owner);
    QSignalSpy frames(&owner, &AudioCapture::pcm), errors(&owner, &AudioCapture::failed);
    const QByteArray audio(2880, 'a');
    const auto old = queue.begin();
    bool accepted = false;
    std::thread producer([&] {
      accepted = queue.append(old, audio.constData(), 2880);
      queue.fail(old, QStringLiteral("old-device-failure"));
    });
    producer.join();
    QVERIFY(accepted);
    queue.stop();
    const auto next = queue.begin();
    QVERIFY(!queue.append(old, audio.constData(), 1920));
    QVERIFY(queue.append(next, nullptr, 960, true));
    QCoreApplication::processEvents();
    QCOMPARE(frames.size(), 0);
    QCOMPARE(errors.size(), 0);
    QVERIFY(queue.append(next, nullptr, 960, true));
    QTRY_COMPARE(frames.size(), 1);
    QCOMPARE(frames.first().first().toByteArray(), QByteArray(1920, '\0'));
  }
  void blockedDeliveryStopsWithinOneSecondBudgetAndNextRoundRecovers()
  {
    FakeCapture owner;
    AudioCaptureQueue queue(owner);
    QSignalSpy frames(&owner, &AudioCapture::pcm), errors(&owner, &AudioCapture::failed);
    const auto run = queue.begin();
    for (int index = 0; index < 50; ++index)
      QVERIFY(queue.append(run, nullptr, 1920, true));
    QVERIFY(!queue.append(run, nullptr, 1920, true));
    QVERIFY(!queue.append(run, nullptr, 1920, true));
    QTRY_COMPARE(errors.size(), 1);
    QCOMPARE(frames.size(), 0);
    const auto next = queue.begin();
    QVERIFY(queue.append(next, nullptr, 1920, true));
    QTRY_COMPARE(frames.size(), 1);
  }
  void malformedPacketsCannotEnterTransport_data()
  {
    QTest::addColumn<int>("size");
    QTest::newRow("empty") << 0;
    QTest::newRow("odd-pcm16") << 1919;
    QTest::newRow("oversized") << 96002;
  }
  void malformedPacketsCannotEnterTransport()
  {
    QFETCH(int, size);
    FakeCapture owner;
    AudioCaptureQueue queue(owner);
    QSignalSpy frames(&owner, &AudioCapture::pcm), errors(&owner, &AudioCapture::failed);
    const auto run = queue.begin();
    QVERIFY(!queue.append(run, nullptr, size, true));
    QTRY_COMPARE(errors.size(), 1);
    QCOMPARE(frames.size(), 0);
  }
};
QTEST_MAIN(AudioCaptureQueueTests)
#include "AudioCaptureQueueTests.moc"
