/*
 * SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "gui/voice/VoiceBackend.h"
#include "gui/voice/VoiceSettingsPage.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QEvent>
#include <QJsonDocument>
#include <QSaveFile>

using namespace deskflow::gui::voice;
class ReleaseGuard : public QObject
{
public:
  VoiceBackend *backend = nullptr;
  bool eventFilter(QObject *, QEvent *event) override
  {
    if (backend && event->type() == QEvent::MouseButtonRelease)
      backend->release();
    if (backend && (event->type() == QEvent::ApplicationDeactivate))
      backend->release();
    return false;
  }
};
int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  app.setApplicationName(QStringLiteral("Deskflow Voice Dev"));
  QCommandLineParser parser;
  parser.addHelpOption();
  parser.addOptions(
      {{QStringLiteral("peer"), QStringLiteral("Private pairing file"), QStringLiteral("file")},
       {QStringLiteral("state"), QStringLiteral("Private counters file"), QStringLiteral("file")},
       {QStringLiteral("input-name"), QStringLiteral("Select exactly one named input; never fall back"),
        QStringLiteral("name")}}
  );
  parser.process(app);
  if (!parser.isSet(QStringLiteral("peer"))) {
    qInfo(
        "Usage: VoiceRuntimePreview --peer <private.json> [--state <private-status.json>] [--input-name <exact-name>]"
    );
    return app.arguments().size() == 1 ? 0 : 2;
  }
  const auto stateFile = parser.value(QStringLiteral("state"));
  try {
    VoiceBackend backend;
    backend.configure(VoicePeer::load(parser.value(QStringLiteral("peer"))));
    SettingsPage page;
    auto preferences = Preferences::defaults();
    preferences.enabled = true;
    if (parser.isSet(QStringLiteral("input-name"))) {
      const auto requested = parser.value(QStringLiteral("input-name"));
      const auto inputs = queryAudioInputs();
      int matches = 0;
      for (const auto &input : inputs.devices) {
        if (input.name == requested) {
          preferences.inputUid = input.uid;
          ++matches;
        }
      }
      if (matches != 1) {
        qCritical("Requested input is missing or ambiguous; no fallback");
        return 1;
      }
    }
    page.loadPreferences(preferences);
    page.bindBackend(&backend);
    page.setWindowTitle(QStringLiteral("跨设备语音 · 普通麦克风开发验证"));
    page.resize(720, 500);
    ReleaseGuard guard;
    guard.backend = &backend;
    app.installEventFilter(&guard);
    quint64 rounds = 0, total = 0;
    const auto state = [&] {
      if (stateFile.isEmpty())
        return;
      QSaveFile file(stateFile);
      if (!file.open(QIODevice::WriteOnly))
        return;
      file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
      file.write(QJsonDocument(
                     QJsonObject{
                         {QStringLiteral("ready"), backend.ready()},
                         {QStringLiteral("busy"), backend.busy()},
                         {QStringLiteral("status"), backend.status()},
                         {QStringLiteral("rounds"), static_cast<qint64>(rounds)},
                         {QStringLiteral("pcm_bytes"), static_cast<qint64>(total)}
                     }
      ).toJson());
      file.commit();
    };
    QObject::connect(&backend, &VoiceBackend::changed, &app, state);
    QObject::connect(&backend, &VoiceBackend::roundEnded, &app, [&](quint64 bytes, const QString &) {
      ++rounds;
      total += bytes;
      state();
    });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &backend, &VoiceBackend::close);
    page.show();
    QTimer::singleShot(200, &backend, &VoiceBackend::connectPeer);
    return app.exec();
  } catch (const std::exception &) {
    qCritical("Cannot load private peer configuration");
    return 1;
  }
}
