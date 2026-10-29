/*
 * SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "AudioCaptureQueue.h"
#include <QElapsedTimer>
#include <system_error>
#include <thread>
#include <windows.h>

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

namespace deskflow::gui::voice {
namespace {
using Microsoft::WRL::ComPtr;
struct Event
{
  HANDLE value = nullptr;
  ~Event()
  {
    if (value)
      CloseHandle(value);
  }
};
struct Apartment
{
  HRESULT status = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  ~Apartment()
  {
    if (SUCCEEDED(status))
      CoUninitialize();
  }
};
} // namespace

class WindowsCapture final : public AudioCapture
{
public:
  using AudioCapture::AudioCapture;
  ~WindowsCapture() override
  {
    stop();
  }
  bool permitted() const override
  {
    // Desktop WASAPI has no per-app permission prompt. Open reports privacy denial.
    return true;
  }
  void requestPermission(std::function<void(bool)> done) override
  {
    done(true);
  }
  bool start(const QString &uid, QString &error) override
  {
    stop();
    m_stop.value = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!m_stop.value) {
      error = QStringLiteral("无法准备麦克风收音");
      return false;
    }
    const auto run = m_delivery.begin();
    try {
      m_worker = std::thread([this, run, uid] { capture(run, uid); });
    } catch (const std::system_error &) {
      stop();
      error = QStringLiteral("无法启动麦克风收音");
      return false;
    }
    return true;
  }
  void stop() override
  {
    m_delivery.stop();
    if (m_stop.value)
      SetEvent(m_stop.value);
    if (m_worker.joinable())
      m_worker.join();
    if (m_stop.value) {
      CloseHandle(m_stop.value);
      m_stop.value = nullptr;
    }
  }

private:
  void capture(const AudioCaptureQueue::Token &run, const QString &uid)
  {
    // All COM interfaces, including GetBuffer/ReleaseBuffer, remain on this thread.
    const Apartment apartment;
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    ComPtr<IMMEndpoint> endpoint;
    ComPtr<IAudioClient> client;
    ComPtr<IAudioCaptureClient> reader;
    const auto check = [this, &run](HRESULT status) {
      if (SUCCEEDED(status))
        return true;
      m_delivery.fail(
          run,
          status == E_ACCESSDENIED
              ? QStringLiteral("请在 Windows 隐私设置中允许桌面应用使用麦克风")
              : QStringLiteral("无法使用所选麦克风（0x%1）").arg(static_cast<quint32>(status), 8, 16, QLatin1Char('0'))
      );
      return false;
    };
    if (!check(apartment.status) ||
        !check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&enumerator))
        ) ||
        !check(
            uid.isEmpty() ? enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device)
                          : enumerator->GetDevice(reinterpret_cast<LPCWSTR>(uid.utf16()), &device)
        ) ||
        !check(device.As(&endpoint)))
      return;
    EDataFlow flow;
    DWORD state = 0;
    if (!check(endpoint->GetDataFlow(&flow)) || !check(device->GetState(&state)))
      return;
    if (flow != eCapture || state != DEVICE_STATE_ACTIVE) {
      m_delivery.fail(run, QStringLiteral("所选麦克风不可用，请重新选择声音来源"));
      return;
    }
    if (!check(device->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER, nullptr, &client)))
      return;
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = 48000;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 2;
    format.nAvgBytesPerSec = 96000;
    const Event available{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    if (!available.value) {
      m_delivery.fail(run, QStringLiteral("无法准备麦克风缓冲区"));
      return;
    }
    // Windows converts the selected capture endpoint to the wire format; no loopback.
    if (!check(client->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
            1000000, 0, &format, nullptr
        )) ||
        !check(client->SetEventHandle(available.value)) || !check(client->GetService(IID_PPV_ARGS(&reader))))
      return;
    if (WaitForSingleObject(m_stop.value, 0) == WAIT_OBJECT_0 || !check(client->Start()))
      return;
    const HANDLE events[]{m_stop.value, available.value};
    QElapsedTimer lastPacket;
    lastPacket.start();
    bool seenPacket = false, running = true;
    while (running) {
      const auto wait = WaitForMultipleObjects(2, events, FALSE, 1000);
      if (wait == WAIT_OBJECT_0)
        break;
      if (wait != WAIT_OBJECT_0 + 1 && wait != WAIT_TIMEOUT) {
        m_delivery.fail(run, QStringLiteral("麦克风等待失败，已停止本轮"));
        break;
      }
      UINT32 frames = 0;
      while (running && WaitForSingleObject(m_stop.value, 0) != WAIT_OBJECT_0 &&
             (running = check(reader->GetNextPacketSize(&frames))) && frames) {
        BYTE *data = nullptr;
        DWORD flags = 0;
        if (!check(reader->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) {
          running = false;
          break;
        }
        // A first-packet discontinuity is normal; a later gap ends this round.
        if (seenPacket && (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)) {
          m_delivery.fail(run, QStringLiteral("麦克风声音中断，请重新连接语音"));
          running = false;
        } else if (!frames || frames > 48000) {
          m_delivery.fail(run, QStringLiteral("麦克风缓冲区无效"));
          running = false;
        } else {
          running = m_delivery.append(
              run, reinterpret_cast<const char *>(data), static_cast<int>(frames * 2),
              (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0
          );
          seenPacket = true;
          lastPacket.restart();
        }
        if (!check(reader->ReleaseBuffer(frames)))
          running = false;
      }
      if (running && lastPacket.elapsed() >= 3000) {
        m_delivery.fail(run, QStringLiteral("未收到麦克风声音，请检查设备和隐私权限"));
        break;
      }
    }
    client->Stop();
  }
  AudioCaptureQueue m_delivery{*this};
  Event m_stop;
  std::thread m_worker;
};

AudioCapture *createAudioCapture(QObject *parent)
{
  return new WindowsCapture(parent);
}
} // namespace deskflow::gui::voice
