/*
 * SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "common/Settings.h"
#include "gui/config/ServerConfig.h"
#include "gui/dialogs/SettingsDialog.h"
#include "gui/voice/VoiceSettingsPage.h"

#include <QApplication>
#include <QCheckBox>
#include <QDebug>
#include <QLabel>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTranslator>

int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  app.setApplicationName(QStringLiteral("Deskflow Voice Native Preview"));
  const auto args = app.arguments();
  if (args.size() == 2 && args.at(1) == QStringLiteral("--probe-devices")) {
    const auto inputs = deskflow::gui::voice::queryAudioInputs();
    // Counts only: never print device UIDs, user-assigned names or serials.
    qInfo(
        "supported=%d inputs=%lld error=%d", inputs.supported, static_cast<long long>(inputs.devices.size()),
        !inputs.error.isEmpty()
    );
    return inputs.supported && inputs.error.isEmpty() ? 0 : 1;
  }
  if (args.size() == 3 && args.at(1) == QStringLiteral("--render-dialog")) {
#ifdef Q_OS_WIN
    qWarning("Full-dialog render is disabled here; use --render without global settings");
    return 2;
#else
    QTemporaryDir profile;
    if (!profile.isValid())
      return 1;
    qputenv("XDG_CONFIG_HOME", (profile.path() + QStringLiteral("/config")).toUtf8());
    qputenv("XDG_STATE_HOME", (profile.path() + QStringLiteral("/state")).toUtf8());
    QTranslator translations;
    const auto path =
        app.applicationDirPath() + QStringLiteral("/Deskflow.app/Contents/Resources/translations/deskflow_zh_CN.qm");
    if (translations.load(path))
      app.installTranslator(&translations);
    ServerConfig server;
    SettingsDialog dialog(nullptr, server);
    if (!Settings::settingsFile().startsWith(profile.path() + QLatin1Char('/')))
      return 1;
    dialog.findChild<QTabWidget *>(QStringLiteral("tabWidget"))
        ->setCurrentWidget(dialog.findChild<deskflow::gui::voice::SettingsPage *>());
    dialog.findChild<QCheckBox *>(QStringLiteral("voiceEnabled"))->setChecked(true);
    dialog.show();
    app.processEvents();
    const auto saved = dialog.grab().save(args.at(2));
    Settings::save(false);
    return saved ? 0 : 1;
#endif
  }
  if (args.size() != 3 || args.at(1) != QStringLiteral("--render")) {
    qInfo("Usage: VoiceSettingsPreview --render <output.png> | --render-dialog <output.png> | --probe-devices");
    return args.size() == 1 ? 0 : 2;
  }
  deskflow::gui::voice::SettingsPage page;
  page.setWindowTitle(QStringLiteral("跨设备语音 · 原生开发预览"));
  page.resize(720, 500);
  page.show();
  app.processEvents();
  return page.grab().save(args.at(2)) ? 0 : 1;
}
