/*
 * SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "gui/voice/VoiceReceiver.h"
#include <QApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>

using deskflow::gui::voice::VoiceReceiver;
namespace {
void event(const char *type, bool ready = true)
{
  std::cout << "{\"details\":{\"type\":\"" << type << "\",\"ready\":" << (ready ? "true" : "false") << "}}"
            << std::endl;
}
int fixture(const QString &config)
{
  event("listening");
  event("authenticated");
  std::string command;
  while (std::getline(std::cin, command)) {
    if (command == "test:start")
      event("started");
    else if (command == "test:end")
      event("ended");
    else if (command == "test:crash")
      return 7;
    else if (command == "stop")
      break;
  }
  event("ended");
  QFile marker(config + QStringLiteral(".stopped"));
  if (!marker.open(QIODevice::WriteOnly))
    return 9;
  marker.write("stopped");
  return 0;
}
} // namespace
class VoiceReceiverTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void missingInstallDoesNotStartChild()
  {
    VoiceReceiver receiver(nullptr, QStringLiteral("missing-receiver-executable"));
    receiver.start(QStringLiteral("missing-config"));
    QVERIFY(!receiver.running());
    QVERIFY(receiver.status().contains(QStringLiteral("安装与配对")));
  }
  void repeatedStartsAreIdempotentAndStopCleansOwnedChild()
  {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto config = directory.filePath(QStringLiteral("fixture.json"));
    QFile file(config);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    VoiceReceiver receiver(nullptr, QCoreApplication::applicationFilePath());
    for (int round = 0; round < 2; ++round) {
      receiver.start(config);
      QTRY_VERIFY_WITH_TIMEOUT(receiver.connected(), 3000);
      auto *child = receiver.findChild<QProcess *>();
      QVERIFY(child);
      const auto pid = child->processId();
      receiver.start(config);
      QCOMPARE(child->processId(), pid);
      child->write("test:start\n");
      QTRY_VERIFY(receiver.recording());
      QVERIFY(receiver.stop());
      QVERIFY(!receiver.running());
      QVERIFY(!receiver.recording());
      QCOMPARE(child->exitCode(), 0);
      QVERIFY(QFile::exists(config + QStringLiteral(".stopped")));
    }
  }
  void unexpectedExitClearsRecordingAndCanRestart()
  {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto config = directory.filePath(QStringLiteral("fixture.json"));
    QFile file(config);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    VoiceReceiver receiver(nullptr, QCoreApplication::applicationFilePath());
    receiver.start(config);
    QTRY_VERIFY(receiver.connected());
    auto *child = receiver.findChild<QProcess *>();
    child->write("test:start\n");
    QTRY_VERIFY(receiver.recording());
    child->write("test:crash\n");
    QTRY_VERIFY(!receiver.running());
    QVERIFY(!receiver.connected());
    QVERIFY(!receiver.recording());
    receiver.start(config);
    QTRY_VERIFY(receiver.connected());
    QVERIFY(receiver.stop());
  }
  void destroyingOwnerUsesGracefulStop()
  {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto config = directory.filePath(QStringLiteral("fixture.json"));
    QFile file(config);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    {
      VoiceReceiver receiver(nullptr, QCoreApplication::applicationFilePath());
      receiver.start(config);
      QTRY_VERIFY(receiver.connected());
      receiver.findChild<QProcess *>()->write("test:start\n");
      QTRY_VERIFY(receiver.recording());
    }
    QVERIFY(QFile::exists(config + QStringLiteral(".stopped")));
  }
};
int main(int argc, char **argv)
{
  // The test's child is only this fixture: no receiver binary, keys or microphone.
  if (argc == 3 && QByteArray(argv[1]) == "--serve-v2-managed")
    return fixture(QString::fromLocal8Bit(argv[2]));
  QApplication app(argc, argv);
  VoiceReceiverTests test;
  return QTest::qExec(&test, argc, argv);
}
#include "VoiceReceiverTests.moc"
