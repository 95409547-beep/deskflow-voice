/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "arch/Arch.h"
#include "base/EventQueue.h"
#include "base/Log.h"
#include "deskflow/unix/AppUtilUnix.h"
#include "platform/IOSXKeyResource.h"
#include "platform/OSXKeyState.h"

#include <QApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QTimer>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

// Exercise the production Qt event loop using public Qt APIs only. No QtTest
// private ABI, event tap, posted key, source selection or clipboard access.
int main(int argc, char **argv)
{
  QTemporaryDir temporary;
  if (!temporary.isValid())
    return 1;
  const auto config = temporary.path() + "/config";
  const auto stateDir = temporary.path() + "/state";
  if (!QDir().mkpath(config + "/Deskflow") || !QDir().mkpath(stateDir))
    return 1;
  qputenv("XDG_CONFIG_HOME", config.toUtf8());
  qputenv("XDG_STATE_HOME", stateDir.toUtf8());
  QApplication app(argc, argv);
  Arch arch;
  arch.init();
  Log log;
  log.setFilter(LogLevel::Level::Error);
  EventQueue events;
  AppUtilUnix util(&events);
  const auto language = util.getCurrentLanguageCode();
  const auto layouts = util.getKeyboardLayoutList();
  const auto legacyKey = IOSXKeyResource::getKeyID(128);
  deskflow::KeyMap referenceMap;
  OSXKeyState reference(&events, referenceMap, {"en"}, true);
  reference.updateKeyMap();
  const auto group = reference.pollActiveGroup();
  auto event = CGEventCreateKeyboardEvent(nullptr, 0, true);
  if (!event || language.empty() || layouts.empty())
    return 1;
  CGEventSetFlags(event, 0);
  OSXKeyState::KeyIDs expectedIds;
  KeyModifierMask expectedMask = 0;
  const auto button = reference.mapKeyFromEvent(expectedIds, &expectedMask, event);
  CFRelease(event);
  if (!button || expectedIds.empty())
    return 1;
  std::atomic<int> done{0}, iterations{0}, failures{0};
  std::vector<std::thread> workers;
  QTimer timeout;
  timeout.setSingleShot(true);
  QObject::connect(&timeout, &QTimer::timeout, [] {
    std::fputs("Qt main-loop input-source probe timed out\n", stderr);
    std::_Exit(2);
  });
  timeout.start(30000);
  QTimer::singleShot(0, &app, [&] {
    for (int worker = 0; worker < 4; ++worker) {
      workers.emplace_back([&] {
        try {
          deskflow::KeyMap map;
          OSXKeyState state(&events, map, {"en"}, true);
          state.updateKeyMap();
          auto key = CGEventCreateKeyboardEvent(nullptr, 0, true);
          if (!key) {
            ++failures;
          } else {
            CGEventSetFlags(key, 0);
            for (int i = 0; i < 1000; ++i) {
              OSXKeyState::KeyIDs ids;
              KeyModifierMask mask = 0;
              if (state.pollActiveGroup() != group || util.getCurrentLanguageCode() != language ||
                  util.getKeyboardLayoutList() != layouts || IOSXKeyResource::getKeyID(128) != legacyKey ||
                  state.mapKeyFromEvent(ids, &mask, key) != button || ids != expectedIds || mask != expectedMask)
                ++failures;
              ++iterations;
            }
            CFRelease(key);
          }
        } catch (...) {
          ++failures;
        }
        if (++done == 4)
          QMetaObject::invokeMethod(&app, "quit", Qt::QueuedConnection);
      });
    }
  });
  app.exec();
  for (auto &worker : workers)
    worker.join();
  std::printf("Qt runtime=%s read_groups=%d failures=%d\n", qVersion(), iterations.load(), failures.load());
  return iterations == 4000 && failures == 0 ? 0 : 1;
}
