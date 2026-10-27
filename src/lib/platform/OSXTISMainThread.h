/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <QCoreApplication>
#include <QMetaObject>
#include <dispatch/dispatch.h>
#include <exception>
#include <pthread.h>
#include <stdexcept>
#include <type_traits>

// AppKit also calls TIS internally: a Deskflow-only mutex cannot serialize it.
// Keep the synchronous operation small and never hold a lock across this call.
template <typename Action> void withTISOnMainThread(Action &&action)
{
  if (pthread_main_np()) {
    action();
    return;
  }

  struct Context
  {
    std::remove_reference_t<Action> &action;
    std::exception_ptr error;
  } context{action, nullptr};

  const auto execute = [](void *raw) {
    auto &context = *static_cast<Context *>(raw);
    try {
      context.action();
    } catch (...) {
      context.error = std::current_exception();
    }
  };
  if (auto *app = QCoreApplication::instance()) {
    // Nested Qt event loops do not necessarily service libdispatch's main
    // queue. Use Qt's main-thread receiver while a Qt application is alive.
    if (!QMetaObject::invokeMethod(app, [&] { execute(&context); }, Qt::BlockingQueuedConnection))
      throw std::runtime_error("could not dispatch input-source access to the Qt main thread");
  } else {
    dispatch_sync_f(dispatch_get_main_queue(), &context, execute);
  }
  if (context.error)
    std::rethrow_exception(context.error);
}
