/*
 * SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include "AudioCapture.h"
#include <QByteArray>
#include <atomic>
#include <memory>

namespace deskflow::gui::voice {
// One producer per run. begin/stop and delivery execute on the owner's thread.
// Each run owns its queue budget, so delayed callbacks cannot enter a new round.
class AudioCaptureQueue
{
  struct Run
  {
    std::atomic<bool> active{true};
    std::atomic<bool> canceled{false};
    std::atomic<int> pending{0};
    QByteArray partial;
  };

public:
  using Token = std::shared_ptr<Run>;
  explicit AudioCaptureQueue(AudioCapture &owner) : m_owner(owner)
  {
  }
  ~AudioCaptureQueue()
  {
    stop();
  }
  Token begin()
  {
    stop();
    return m_run = std::make_shared<Run>();
  }
  void stop()
  {
    if (m_run) {
      m_run->active = false;
      m_run->canceled = true;
    }
    m_run.reset();
  }
  void fail(const Token &run, const QString &error)
  {
    if (!run->active.exchange(false))
      return;
    QMetaObject::invokeMethod(
        &m_owner,
        [owner = &m_owner, run, error] {
          if (!run->canceled)
            Q_EMIT owner->failed(error);
        },
        Qt::QueuedConnection
    );
  }
  bool append(const Token &run, const char *data, int size, bool silent = false)
  {
    if (!run->active)
      return false;
    if (size <= 0 || size > 96000 || (size & 1) || (!silent && !data)) {
      fail(run, QStringLiteral("麦克风音频格式无效"));
      return false;
    }
    run->partial.append(silent ? QByteArray(size, '\0') : QByteArray(data, size));
    while (run->partial.size() >= 1920 && run->active) {
      constexpr int frameBytes = 1920;
      if (run->pending.fetch_add(frameBytes) + frameBytes > 96000) {
        run->pending -= frameBytes;
        fail(run, QStringLiteral("收音处理过慢，已停止本轮"));
        return false;
      }
      const auto bytes = run->partial.first(frameBytes);
      run->partial.remove(0, frameBytes);
      QMetaObject::invokeMethod(
          &m_owner,
          [owner = &m_owner, run, bytes] {
            run->pending -= frameBytes;
            if (run->active)
              Q_EMIT owner->pcm(bytes);
          },
          Qt::QueuedConnection
      );
    }
    return run->active;
  }

private:
  AudioCapture &m_owner;
  Token m_run;
};
} // namespace deskflow::gui::voice
