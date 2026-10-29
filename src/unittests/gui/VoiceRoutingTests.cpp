/*
 * SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "VoiceTestSupport.h"
#include "common/Settings.h"
#include "gui/config/ServerConfig.h"
#include "gui/core/CoreProcess.h"
#include "gui/voice/VoiceRouting.h"
#include "gui/voice/VoiceService.h"
#include "gui/voice/VoiceSettingsPage.h"
#include "voice/control/ControlServer.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLocalSocket>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>

using namespace deskflow::voice;
using namespace deskflow::voice::control;
namespace {
QString controlName(const QTemporaryDir &directory)
{
#ifdef Q_OS_WIN
  Q_UNUSED(directory);
  return QStringLiteral("deskflow-voice-test-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
#else
  return directory.filePath(QStringLiteral("control"));
#endif
}
} // namespace
class VoiceRoutingTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase()
  {
    QVERIFY(m_isolated.isValid());
#ifndef Q_OS_WIN
    qputenv("XDG_CONFIG_HOME", m_isolated.filePath(QStringLiteral("config")).toUtf8());
    qputenv("XDG_STATE_HOME", m_isolated.filePath(QStringLiteral("state")).toUtf8());
#endif
  }
  void privateFramesAndProofsAreBoundedAndRoleSpecific()
  {
    const auto secret = newSecret();
    QCOMPARE(secret.size(), 32);
    const auto server = proof(
        secret, QStringLiteral("server"), QStringLiteral("nonce"), QStringLiteral("epoch"), QStringLiteral("owner"), 42
    );
    QVERIFY(equalProof(server, server));
    QVERIFY(!equalProof(
        server, proof(
                    secret, QStringLiteral("client"), QStringLiteral("nonce"), QStringLiteral("epoch"),
                    QStringLiteral("owner"), 42
                )
    ));
    QVERIFY(!equalProof(
        server, proof(
                    newSecret(), QStringLiteral("server"), QStringLiteral("nonce"), QStringLiteral("epoch"),
                    QStringLiteral("owner"), 42
                )
    ));
    Frames frames;
    const QJsonObject expected{{QStringLiteral("type"), QStringLiteral("hello")}};
    const auto bytes = Frames::encode(expected);
    QVERIFY(frames.append(bytes.left(2)).isEmpty());
    QCOMPARE(frames.append(bytes.mid(2)).first(), expected);
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, frames.append(QByteArray::fromHex("00002001")));
  }
  void realPrivateIpcRoutesOnlyPairedScreenAndRepeats()
  {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto secret = newSecret();
    const auto name = controlName(directory);
    const auto pid = QCoreApplication::applicationPid();
    ControlServer core(nullptr, secret, name, pid);
    QVERIFY(core.listen());
    core.context({"Mac", false, true});
    LoopbackReceiver receiver;
    auto peer = receiver.peer();
    peer.screen = QStringLiteral("Windows");
    auto *capture = new FakeCapture;
    VoiceBackend backend(nullptr, capture);
    backend.configure(peer);
    VoiceRouting routing(backend);
    routing.configure(true, peer.screen, QStringLiteral("F8"));
    routing.endpoint(name, secret, pid);
    backend.connectPeer();
    QTRY_VERIFY_WITH_TIMEOUT(backend.ready() && routing.armed(), 7000);
    QVERIFY(!core.down(HoldKey::F8, 8, true));
    core.up(8);
    QCOMPARE(capture->starts, 0);
    core.context({"Windows", true, true});
    QTRY_VERIFY_WITH_TIMEOUT(backend.routeEligible(), 3000);
    for (int index = 0; index < 3; ++index) {
      QVERIFY(core.down(HoldKey::F8, 8, true));
      QVERIFY(core.repeat(8));
      QTRY_COMPARE_WITH_TIMEOUT(receiver.begins, index + 1, 3000);
      capture->frame();
      QVERIFY(core.up(8));
      QTRY_VERIFY_WITH_TIMEOUT(!backend.busy() && !capture->active, 3000);
    }
    QCOMPARE(capture->starts, 3);
    QCOMPARE(capture->stops, 3);
    core.context({"OtherWindows", true, true});
    QTRY_VERIFY_WITH_TIMEOUT(!backend.routeEligible(), 3000);
    QVERIFY(!core.down(HoldKey::F8, 8, true));
    core.up(8);
    QCOMPARE(capture->starts, 3);
#ifndef Q_OS_WIN
    const auto permissions = QFileInfo(name).permissions();
    QVERIFY(
        !(permissions &
          (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ReadOther | QFileDevice::WriteOther))
    );
#endif
    routing.detach();
  }
  void routeChangeEndsAndNeverAdoptsHeldKey()
  {
    QTemporaryDir directory;
    const auto secret = newSecret();
    const auto name = controlName(directory);
    const auto pid = QCoreApplication::applicationPid();
    ControlServer core(nullptr, secret, name, pid);
    QVERIFY(core.listen());
    core.context({"Windows", true, true});
    LoopbackReceiver receiver;
    auto peer = receiver.peer();
    peer.screen = QStringLiteral("Windows");
    auto *capture = new FakeCapture;
    VoiceBackend backend(nullptr, capture);
    backend.configure(peer);
    VoiceRouting routing(backend);
    routing.configure(true, peer.screen, QStringLiteral("F8"));
    routing.endpoint(name, secret, pid);
    backend.connectPeer();
    QTRY_VERIFY_WITH_TIMEOUT(routing.armed() && backend.routeEligible(), 7000);
    QVERIFY(core.down(HoldKey::F8, 8, true));
    QTRY_COMPARE_WITH_TIMEOUT(receiver.begins, 1, 3000);
    core.context({"Mac", false, true});
    QTRY_VERIFY_WITH_TIMEOUT(!capture->active && !backend.busy(), 3000);
    core.context({"Windows", true, true});
    QTRY_VERIFY_WITH_TIMEOUT(backend.routeEligible(), 3000);
    QVERIFY(core.down(HoldKey::F8, 8, true));
    QTest::qWait(30);
    QCOMPARE(capture->starts, 1);
    QVERIFY(core.up(8));
    QTest::qWait(30);
    QVERIFY(core.down(HoldKey::F8, 8, true));
    QTRY_COMPARE_WITH_TIMEOUT(receiver.begins, 2, 3000);
    core.up(8);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
    routing.detach();
  }
  void controlOwnerExitStopsAndReconnectDoesNotReplay()
  {
    QTemporaryDir directory;
    const auto secret = newSecret();
    const auto name = controlName(directory);
    const auto pid = QCoreApplication::applicationPid();
    ControlServer core(nullptr, secret, name, pid);
    QVERIFY(core.listen());
    core.context({"Windows", true, true});
    LoopbackReceiver receiver;
    auto peer = receiver.peer();
    peer.screen = QStringLiteral("Windows");
    auto *capture = new FakeCapture;
    VoiceBackend backend(nullptr, capture);
    backend.configure(peer);
    VoiceRouting routing(backend);
    routing.configure(true, peer.screen, QStringLiteral("F8"));
    routing.endpoint(name, secret, pid);
    backend.connectPeer();
    QTRY_VERIFY_WITH_TIMEOUT(routing.armed() && backend.routeEligible(), 7000);
    core.down(HoldKey::F8, 8, true);
    QTRY_COMPARE_WITH_TIMEOUT(receiver.begins, 1, 3000);
    routing.detach();
    QTRY_VERIFY_WITH_TIMEOUT(!capture->active && !backend.busy(), 3000);
    routing.endpoint(name, secret, pid);
    QTRY_VERIFY_WITH_TIMEOUT(routing.armed(), 3000);
    QCOMPARE(capture->starts, 1);
    core.up(8);
    QTest::qWait(30);
    core.down(HoldKey::F8, 8, true);
    QTRY_COMPARE_WITH_TIMEOUT(receiver.begins, 2, 3000);
    core.up(8);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
    routing.detach();
  }
  void unknownControlSecretNeverInterceptsOrRecords()
  {
    QTemporaryDir directory;
    const auto secret = newSecret();
    const auto name = controlName(directory);
    const auto pid = QCoreApplication::applicationPid();
    ControlServer core(nullptr, secret, name, pid);
    QVERIFY(core.listen());
    core.context({"Windows", true, true});
    LoopbackReceiver receiver;
    auto peer = receiver.peer();
    peer.screen = QStringLiteral("Windows");
    auto *capture = new FakeCapture;
    VoiceBackend backend(nullptr, capture);
    backend.configure(peer);
    VoiceRouting routing(backend);
    routing.configure(true, peer.screen, QStringLiteral("F8"));
    QSignalSpy changes(&routing, &VoiceRouting::changed);
    routing.endpoint(name, newSecret(), pid);
    changes.clear();
    backend.connectPeer();
    QTRY_VERIFY_WITH_TIMEOUT(backend.ready() && changes.count() > 0, 7000);
    QVERIFY(!routing.connected());
    QVERIFY(!backend.inputReady());
    QVERIFY(!core.down(HoldKey::F8, 8, true));
    core.up(8);
    backend.press();
    QCOMPARE(capture->starts, 0);
    QCOMPARE(receiver.begins, 0);
    routing.detach();
  }

  void voiceReconnectStopsCaptureAndRequiresFreshPress()
  {
    QTemporaryDir directory;
    const auto secret = newSecret();
    const auto name = controlName(directory);
    const auto pid = QCoreApplication::applicationPid();
    ControlServer core(nullptr, secret, name, pid);
    QVERIFY(core.listen());
    core.context({"Windows", true, true});
    LoopbackReceiver receiver;
    auto peer = receiver.peer();
    peer.screen = QStringLiteral("Windows");
    auto *capture = new FakeCapture;
    VoiceBackend backend(nullptr, capture);
    backend.configure(peer);
    VoiceRouting routing(backend);
    routing.configure(true, peer.screen, QStringLiteral("F8"));
    routing.endpoint(name, secret, pid);
    backend.connectPeer();
    QTRY_VERIFY_WITH_TIMEOUT(routing.armed() && backend.routeEligible(), 7000);
    QVERIFY(core.down(HoldKey::F8, 8, true));
    QTRY_COMPARE_WITH_TIMEOUT(receiver.begins, 1, 3000);
    receiver.client->abort();
    QTRY_VERIFY_WITH_TIMEOUT(!backend.ready() && !capture->active && !backend.busy(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!routing.armed(), 3000);
    backend.connectPeer();
    QTRY_VERIFY_WITH_TIMEOUT(backend.ready() && routing.armed() && backend.routeEligible(), 7000);
    core.repeat(8);
    QTest::qWait(30);
    QCOMPARE(capture->starts, 1);
    QCOMPARE(receiver.begins, 1);
    core.up(8);
    QTest::qWait(30);
    QVERIFY(core.down(HoldKey::F8, 8, true));
    QTRY_COMPARE_WITH_TIMEOUT(receiver.begins, 2, 3000);
    core.up(8);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy() && !capture->active, 3000);
    routing.detach();
  }

  void coreRestartRequiresFreshPressAndNewAuthentication()
  {
    QTemporaryDir directory;
    const auto pid = QCoreApplication::applicationPid();
    const auto name = controlName(directory);
    const auto secret = newSecret();
    auto core = std::make_unique<ControlServer>(nullptr, secret, name, pid);
    QVERIFY(core->listen());
    core->context({"Windows", true, true});
    LoopbackReceiver receiver;
    auto peer = receiver.peer();
    peer.screen = QStringLiteral("Windows");
    auto *capture = new FakeCapture;
    VoiceBackend backend(nullptr, capture);
    backend.configure(peer);
    VoiceRouting routing(backend);
    routing.configure(true, peer.screen, QStringLiteral("F8"));
    routing.endpoint(name, secret, pid);
    backend.connectPeer();
    QTRY_VERIFY_WITH_TIMEOUT(routing.armed() && backend.routeEligible(), 7000);
    core->down(HoldKey::F8, 8, true);
    QTRY_COMPARE_WITH_TIMEOUT(receiver.begins, 1, 3000);
    core.reset();
    QTRY_VERIFY_WITH_TIMEOUT(!capture->active && !backend.busy() && !backend.inputReady(), 3000);
    const auto replacementSecret = newSecret();
    core = std::make_unique<ControlServer>(nullptr, replacementSecret, name, pid);
    QVERIFY(core->listen());
    core->context({"Windows", true, true});
    routing.endpoint(name, replacementSecret, pid);
    QTRY_VERIFY_WITH_TIMEOUT(routing.armed() && backend.routeEligible(), 3000);
    QVERIFY(!core->repeat(8)); // a held repeat from the old core never becomes a fresh DOWN
    QVERIFY(!core->up(8));
    QTest::qWait(30);
    QCOMPARE(capture->starts, 1);
    QVERIFY(core->down(HoldKey::F8, 8, true));
    QTRY_COMPARE_WITH_TIMEOUT(receiver.begins, 2, 3000);
    core->up(8);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
    routing.detach();
  }

  void productSettingsDraftDoesNotChangeLiveCaptureOrConnection()
  {
    LoopbackReceiver receiver;
    auto *capture = new FakeCapture;
    VoiceBackend backend(nullptr, capture);
    backend.configure(receiver.peer());
    backend.connectPeer();
    QTRY_VERIFY_WITH_TIMEOUT(backend.ready(), 7000);
    SettingsPage page(nullptr, [] {
      return AudioInputs{
          true,
          {{QStringLiteral("mic-a"), QStringLiteral("Mic A"), true},
           {QStringLiteral("mic-b"), QStringLiteral("Mic B"), false}},
          {}
      };
    });
    auto prefs = Preferences::defaults();
    prefs.enabled = true;
    prefs.inputUid = QStringLiteral("mic-a");
    page.loadPreferences(prefs);
    page.bindBackend(&backend, false);
    backend.press();
    QTRY_COMPARE_WITH_TIMEOUT(receiver.begins, 1, 3000);
    page.findChild<QComboBox *>(QStringLiteral("voiceInput"))->setCurrentIndex(1);
    page.findChild<QCheckBox *>(QStringLiteral("voiceEnabled"))->setChecked(false);
    QVERIFY(page.isModified());
    QVERIFY(backend.ready() && capture->active);
    backend.release();
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
  }

  void serviceRequiresSavedPairAndCoreAndUnchangedApplyDoesNotReconnect()
  {
#ifndef Q_OS_MACOS
    QSKIP("Main GUI sender is currently macOS only; IPC and model tests run on all platforms");
#else
    QVERIFY(Settings::settingsFile().startsWith(m_isolated.path() + QLatin1Char('/')));
    LoopbackReceiver receiver;
    const auto peer = receiver.peer();
    const auto path = m_isolated.filePath(QStringLiteral("peer.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(
                   QJsonObject{
                       {QStringLiteral("host"), peer.host},
                       {QStringLiteral("port"), peer.port},
                       {QStringLiteral("certificate_sha256"), QString::fromLatin1(peer.certificateSha256.toHex())},
                       {QStringLiteral("token"), peer.token},
                       {QStringLiteral("screen"), QStringLiteral("Windows")}
                   }
    ).toJson());
    file.close();
    Settings::setValue(Settings::Voice::PeerFile, path);
    Settings::setValue(Settings::Voice::Enabled, false);
    ServerConfig server;
    deskflow::gui::CoreProcess core(server); // never starts a core or installs a key hook
    auto *capture = new FakeCapture;
    VoiceService service(core, nullptr, capture);
    service.apply();
    QTest::qWait(20);
    QCOMPARE(receiver.hellos, 0);
    Settings::setValue(Settings::Voice::Enabled, true);
    service.apply();
    QTRY_VERIFY_WITH_TIMEOUT(service.backend()->ready(), 7000);
    QVERIFY(!service.backend()->inputReady());
    service.backend()->press();
    QCOMPARE(capture->starts, 0);
    QCOMPARE(receiver.begins, 0);
    service.apply();
    QTest::qWait(30);
    QCOMPARE(receiver.hellos, 1);
    receiver.client->abort();
    QTRY_VERIFY_WITH_TIMEOUT(!service.backend()->ready(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(service.backend()->ready() && receiver.hellos == 2, 7000);
    QCOMPARE(capture->starts, 0);
    QCOMPARE(receiver.begins, 0);
    Settings::setValue(Settings::Voice::Enabled, false);
    service.apply();
    QVERIFY(!service.backend()->ready());
    QTest::qWait(1200);
    QCOMPARE(receiver.hellos, 2);
    Settings::setValue(Settings::Voice::Enabled, true);
    service.apply();
    QTRY_VERIFY_WITH_TIMEOUT(service.backend()->ready() && receiver.hellos == 3, 7000);
    receiver.client->abort();
    QTRY_VERIFY_WITH_TIMEOUT(!service.backend()->ready(), 3000);
    service.shutdown();
    QTest::qWait(1200);
    QCOMPARE(receiver.hellos, 3);
    QCOMPARE(capture->starts, 0);
#endif
  }

  void missingMicrophoneOffersSourceSelectionEvenWhenReceiverIsReady()
  {
    LoopbackReceiver receiver;
    auto *capture = new FakeCapture;
    VoiceBackend backend(nullptr, capture);
    backend.configure(receiver.peer());
    backend.connectPeer();
    QTRY_VERIFY_WITH_TIMEOUT(backend.ready(), 7000);
    SettingsPage page(nullptr, [] { return AudioInputs{true, {}, {}}; });
    auto prefs = Preferences::defaults();
    prefs.enabled = true;
    prefs.inputUid = QStringLiteral("unplugged-microphone");
    page.loadPreferences(prefs);
    page.bindBackend(&backend, false);
    QCOMPARE(page.findChild<QLabel *>(QStringLiteral("voiceStatus"))->text(), QStringLiteral("连接麦克风"));
    page.findChild<QPushButton *>(QStringLiteral("voicePrimaryAction"))->click();
    QCOMPARE(page.findChild<QTabWidget *>(QStringLiteral("voicePages"))->currentIndex(), 1);
    QCOMPARE(capture->starts, 0);
    QCOMPARE(receiver.begins, 0);
    QVERIFY(backend.ready());
  }

private:
  QTemporaryDir m_isolated;
};
QTEST_MAIN(VoiceRoutingTests)
#include "VoiceRoutingTests.moc"
