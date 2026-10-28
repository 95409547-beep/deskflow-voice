/*
 * SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "AudioCapture.h"
#import <AVFoundation/AVFoundation.h>
#include <AudioToolbox/AudioToolbox.h>
#include <QPointer>
#include <atomic>

namespace deskflow::gui::voice {
class MacCapture final : public AudioCapture
{
public:
  using AudioCapture::AudioCapture;
  ~MacCapture() override
  {
    stop();
  }
  bool permitted() const override
  {
    return [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio] == AVAuthorizationStatusAuthorized;
  }
  void requestPermission(std::function<void(bool)> done) override
  {
    const QPointer<MacCapture> self(this);
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                             completionHandler:^(BOOL granted) {
                               if (self)
                                 QMetaObject::invokeMethod(
                                     self,
                                     [self, done, granted] {
                                       if (self)
                                         done(granted);
                                     },
                                     Qt::QueuedConnection
                                 );
                             }];
  }
  bool start(const QString &uid, QString &error) override
  {
    stop();
    if (!permitted()) {
      error = QStringLiteral("请允许此开发应用使用麦克风");
      return false;
    }
    AudioStreamBasicDescription format{};
    format.mSampleRate = 48000;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    format.mBytesPerPacket = format.mBytesPerFrame = 2;
    format.mFramesPerPacket = format.mChannelsPerFrame = 1;
    format.mBitsPerChannel = 16;
    auto status = AudioQueueNewInput(&format, callback, this, nullptr, nullptr, 0, &m_queue);
    if (status == noErr && !uid.isEmpty()) {
      CFStringRef device = uid.toCFString();
      status = AudioQueueSetProperty(m_queue, kAudioQueueProperty_CurrentDevice, &device, sizeof(device));
      CFRelease(device);
    }
    for (int index = 0; status == noErr && index < 6; ++index) {
      AudioQueueBufferRef buffer = nullptr;
      status = AudioQueueAllocateBuffer(m_queue, 1920, &buffer);
      if (status == noErr)
        status = AudioQueueEnqueueBuffer(m_queue, buffer, 0, nullptr);
    }
    m_active = status == noErr;
    ++m_generation;
    if (status == noErr)
      status = AudioQueueStart(m_queue, nullptr);
    if (status != noErr) {
      stop();
      error = QStringLiteral("无法打开所选麦克风（%1）").arg(status);
      return false;
    }
    return true;
  }
  void stop() override
  {
    m_active = false;
    ++m_generation;
    if (m_queue) {
      AudioQueueStop(m_queue, true);
      AudioQueueDispose(m_queue, true);
      m_queue = nullptr;
    }
  }

private:
  static void callback(
      void *context, AudioQueueRef queue, AudioQueueBufferRef buffer, const AudioTimeStamp *, UInt32,
      const AudioStreamPacketDescription *
  )
  {
    auto *self = static_cast<MacCapture *>(context);
    if (!self->m_active)
      return;
    const auto generation = self->m_generation.load();
    const int size = static_cast<int>(buffer->mAudioDataByteSize);
    if (size > 0 && (size & 1) == 0) {
      if (self->m_pending.fetch_add(size) + size <= 96000) {
        const QByteArray bytes(static_cast<const char *>(buffer->mAudioData), size);
        QMetaObject::invokeMethod(
            self,
            [self, bytes, generation, size] {
              self->m_pending -= size;
              if (self->m_active && self->m_generation == generation)
                Q_EMIT self->pcm(bytes);
            },
            Qt::QueuedConnection
        );
      } else {
        self->m_pending -= size;
        if (self->m_active.exchange(false))
          QMetaObject::invokeMethod(
              self, [self] { Q_EMIT self->failed(QStringLiteral("收音处理过慢，已停止本轮")); }, Qt::QueuedConnection
          );
      }
    }
    if (self->m_active && AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr) != noErr) {
      if (self->m_active.exchange(false))
        QMetaObject::invokeMethod(
            self, [self] { Q_EMIT self->failed(QStringLiteral("声音设备已断开")); }, Qt::QueuedConnection
        );
    }
  }
  AudioQueueRef m_queue = nullptr;
  std::atomic<bool> m_active{false};
  std::atomic<int> m_pending{0};
  std::atomic<unsigned> m_generation{0};
};
AudioCapture *createAudioCapture(QObject *parent)
{
  return new MacCapture(parent);
}
} // namespace deskflow::gui::voice
