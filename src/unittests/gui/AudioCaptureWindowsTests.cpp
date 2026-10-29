/*
 * SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "gui/voice/AudioCapture.h"
#include <QSignalSpy>
#include <QTest>
#include <QUuid>
#include <memory>

using namespace deskflow::gui::voice;
class AudioCaptureWindowsTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void missingSelectedEndpointFailsWithoutDefaultFallbackAndCanRepeat()
  {
    std::unique_ptr<AudioCapture> capture(createAudioCapture(nullptr));
    QSignalSpy frames(capture.get(), &AudioCapture::pcm), errors(capture.get(), &AudioCapture::failed);
    const auto missing = QStringLiteral("deskflow-voice-missing-%1").arg(QUuid::createUuid().toString());
    for (int round = 1; round <= 2; ++round) {
      QString error;
      QVERIFY2(capture->start(missing, error), qPrintable(error));
      QTRY_COMPARE_WITH_TIMEOUT(errors.size(), round, 5000);
      capture->stop();
      QCOMPARE(frames.size(), 0);
    }
  }
  void releaseDuringInitializationSuppressesQueuedFailure()
  {
    std::unique_ptr<AudioCapture> capture(createAudioCapture(nullptr));
    QSignalSpy frames(capture.get(), &AudioCapture::pcm), errors(capture.get(), &AudioCapture::failed);
    for (int round = 0; round < 10; ++round) {
      QString error;
      const auto missing = QStringLiteral("deskflow-voice-missing-%1").arg(QUuid::createUuid().toString());
      QVERIFY2(capture->start(missing, error), qPrintable(error));
      capture->stop();
    }
    QCoreApplication::processEvents();
    QCOMPARE(frames.size(), 0);
    QCOMPARE(errors.size(), 0);
  }
};
QTEST_GUILESS_MAIN(AudioCaptureWindowsTests)
#include "AudioCaptureWindowsTests.moc"
