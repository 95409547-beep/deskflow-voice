/*
 * SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "common/Settings.h"
#include "gui/config/ServerConfig.h"
#include "gui/dialogs/SettingsDialog.h"
#include "gui/voice/VoiceSettingsPage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>

using namespace deskflow::gui::voice;

class VoiceSettingsTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase()
  {
    QVERIFY(m_isolated.isValid());
#ifndef Q_OS_WIN
    // Settings honors these before its first singleton construction. Windows
    // keeps full-dialog cases skipped; the widget/model cases need no singleton.
    qputenv("XDG_CONFIG_HOME", (m_isolated.path() + QStringLiteral("/config")).toUtf8());
    qputenv("XDG_STATE_HOME", (m_isolated.path() + QStringLiteral("/state")).toUtf8());
#endif
  }

  void preferencesRoundTripUsesExplicitFile()
  {
    const auto path = m_isolated.filePath(QStringLiteral("voice-roundtrip.ini"));
    auto wanted = Preferences::defaults();
    wanted.enabled = true;
    wanted.role = QStringLiteral("send");
    wanted.inputUid = QStringLiteral("fixture-mic-uid");
    wanted.targetComputer = QStringLiteral("fixture-peer");
    {
      QSettings file(path, QSettings::IniFormat);
      const auto values = wanted.toValues();
      for (auto it = values.cbegin(); it != values.cend(); ++it) {
        QVERIFY(Settings::validKeys().contains(it.key()));
        file.setValue(it.key(), it.value());
      }
      file.sync();
      QCOMPARE(file.status(), QSettings::NoError);
    }
    QSettings reopened(path, QSettings::IniFormat);
    QVariantMap values;
    for (const auto &key : reopened.allKeys())
      values.insert(key, reopened.value(key));
    QVERIFY(Preferences::fromValues(values) == wanted);
  }

  void missingDeviceNeverFallsBackSilently()
  {
    SettingsPage page(nullptr, [] {
      return AudioInputs{true, {{QStringLiteral("default"), QStringLiteral("Fixture mic"), true}}, {}};
    });
    auto prefs = Preferences::defaults();
    prefs.inputUid = QStringLiteral("removed-device");
    page.loadPreferences(prefs);
    QVERIFY(!page.isModified());
    page.findChild<QPushButton *>(QStringLiteral("voiceRefreshDevices"))->click();
    QCOMPARE(page.preferences().inputUid, QStringLiteral("removed-device"));
    QVERIFY(!page.isModified());
    QVERIFY(!page.serviceReady());
  }

  void keyboardAndMicrophoneDoNotProveVoiceReady()
  {
    SettingsPage page(nullptr, [] {
      return AudioInputs{true, {{QStringLiteral("mic"), QStringLiteral("Fixture mic"), true}}, {}};
    });
    auto prefs = Preferences::defaults();
    prefs.enabled = true;
    page.loadPreferences(prefs);
    page.setConnectionSnapshot({true, true, true, {QStringLiteral("fixture-peer")}});
    QCOMPARE(page.findChild<QLabel *>(QStringLiteral("voiceControlConnection"))->text(), QStringLiteral("键鼠已连接"));
    QVERIFY(!page.serviceReady());
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    QVERIFY(page.findChild<QLabel *>(QStringLiteral("voiceStatus"))->text().contains(QStringLiteral("未就绪")));
#else
    QVERIFY(page.findChild<QLabel *>(QStringLiteral("voiceStatus"))->text().contains(QStringLiteral("不支持")));
#endif
    page.setConnectionSnapshot({false, false, true, {QStringLiteral("stale-peer")}});
    auto *target = page.findChild<QComboBox *>(QStringLiteral("voiceTarget"));
    QCOMPARE(target->findData(QStringLiteral("stale-peer")), -1);
  }

  void readOnlyControlsCannotChangeIntent()
  {
    SettingsPage page(nullptr, [] { return AudioInputs{}; });
    page.setWritable(false);
    const auto before = page.preferences();
    page.findChild<QCheckBox *>(QStringLiteral("voiceEnabled"))->click();
    page.findChild<QPushButton *>(QStringLiteral("voicePrimaryAction"))->click();
    QVERIFY(page.preferences() == before);
    QVERIFY(!page.findChild<QComboBox *>(QStringLiteral("voiceInput"))->isEnabled());
  }

  void resetDoesNotRewriteSavedBaseline()
  {
    SettingsPage page(nullptr, [] { return AudioInputs{}; });
    auto original = Preferences::defaults();
    original.enabled = true;
    original.inputUid = QStringLiteral("wanted-source");
    page.loadPreferences(original);
    page.resetToDefaults();
    QVERIFY(page.isDefault());
    QVERIFY(page.isModified());
    page.loadPreferences(original);
    QVERIFY(!page.isModified());
    QCOMPARE(page.preferences().inputUid, original.inputUid);
  }

  void voiceOnlyDialogSaveDoesNotRequestCoreRestart()
  {
#ifdef Q_OS_WIN
    QSKIP("Full-dialog global settings isolation is not available on Windows; model/widget checks remain active");
#else
    ServerConfig server;
    QVERIFY(Settings::settingsFile().startsWith(m_isolated.path() + QLatin1Char('/')));
    SettingsDialog dialog(nullptr, server);
    auto *enabled = dialog.findChild<QCheckBox *>(QStringLiteral("voiceEnabled"));
    QVERIFY(enabled);
    enabled->setChecked(true);
    QVERIFY(QMetaObject::invokeMethod(&dialog, "accept", Qt::DirectConnection));
    QVERIFY(!dialog.requiresCoreRestart());
    Settings::save(false);
    QSettings saved(Settings::settingsFile(), QSettings::IniFormat);
    QVERIFY(saved.value(Settings::Voice::Enabled).toBool());
#endif
  }

  void cancelKeepsSavedVoicePreference()
  {
#ifdef Q_OS_WIN
    QSKIP("Full-dialog global settings isolation is not available on Windows");
#else
    ServerConfig server;
    SettingsDialog dialog(nullptr, server);
    auto *enabled = dialog.findChild<QCheckBox *>(QStringLiteral("voiceEnabled"));
    enabled->setChecked(false);
    QVERIFY(QMetaObject::invokeMethod(&dialog, "reject", Qt::DirectConnection));
    QVERIFY(Settings::value(Settings::Voice::Enabled).toBool());
#endif
  }

  void generalChangesStillRequestCoreRestart()
  {
#ifdef Q_OS_WIN
    QSKIP("Full-dialog global settings isolation is not available on Windows");
#else
    ServerConfig server;
    SettingsDialog dialog(nullptr, server);
    auto *port = dialog.findChild<QSpinBox *>(QStringLiteral("sbPort"));
    QVERIFY(port);
    port->setValue(port->value() == 24800 ? 24801 : 24800);
    QVERIFY(QMetaObject::invokeMethod(&dialog, "accept", Qt::DirectConnection));
    QVERIFY(dialog.requiresCoreRestart());
#endif
  }

  void cleanupTestCase()
  {
#ifndef Q_OS_WIN
    Settings::save(false);
#endif
  }

private:
  QTemporaryDir m_isolated;
};

QTEST_MAIN(VoiceSettingsTests)
#include "VoiceSettingsTests.moc"
