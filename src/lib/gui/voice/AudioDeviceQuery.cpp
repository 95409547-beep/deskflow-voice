/*
 * SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "AudioDeviceQuery.h"

#include <algorithm>
#include <vector>

#if defined(Q_OS_MACOS)
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#elif defined(Q_OS_WIN)
#include <windows.h>

// The property-key catalog requires DEFINE_PROPERTYKEY before inclusion.
#include <propkeydef.h>

#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <propsys.h>
#include <propvarutil.h>
#include <wrl/client.h>
#endif

namespace deskflow::gui::voice {

#if defined(Q_OS_MACOS)
namespace {
QString deviceString(AudioObjectID device, AudioObjectPropertySelector selector)
{
  AudioObjectPropertyAddress address{selector, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
  CFStringRef value = nullptr;
  UInt32 size = sizeof(value);
  if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &value) != noErr || !value)
    return {};
  const auto result = QString::fromCFString(value);
  CFRelease(value);
  return result;
}
} // namespace
#endif

AudioInputs queryAudioInputs()
{
  AudioInputs result;
#if defined(Q_OS_MACOS)
  result.supported = true;
  AudioObjectPropertyAddress address{
      kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain
  };
  UInt32 size = 0;
  if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, nullptr, &size) != noErr ||
      size % sizeof(AudioDeviceID) != 0) {
    result.error = QStringLiteral("无法读取系统声音设备");
    return result;
  }
  std::vector<AudioDeviceID> devices(size / sizeof(AudioDeviceID));
  if (size &&
      AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size, devices.data()) != noErr) {
    result.error = QStringLiteral("声音设备发生变化，请重新检测");
    return result;
  }
  devices.resize(size / sizeof(AudioDeviceID));
  AudioDeviceID defaultDevice = kAudioObjectUnknown;
  AudioObjectPropertyAddress defaultAddress{
      kAudioHardwarePropertyDefaultInputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain
  };
  UInt32 defaultSize = sizeof(defaultDevice);
  AudioObjectGetPropertyData(kAudioObjectSystemObject, &defaultAddress, 0, nullptr, &defaultSize, &defaultDevice);
  for (const auto device : devices) {
    AudioObjectPropertyAddress aliveAddress{
        kAudioDevicePropertyDeviceIsAlive, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain
    };
    UInt32 alive = 0;
    UInt32 aliveSize = sizeof(alive);
    if (AudioObjectGetPropertyData(device, &aliveAddress, 0, nullptr, &aliveSize, &alive) != noErr || !alive)
      continue;
    AudioObjectPropertyAddress streams{
        kAudioDevicePropertyStreams, kAudioObjectPropertyScopeInput, kAudioObjectPropertyElementMain
    };
    UInt32 streamSize = 0;
    if (AudioObjectGetPropertyDataSize(device, &streams, 0, nullptr, &streamSize) != noErr || streamSize == 0)
      continue;
    const auto uid = deviceString(device, kAudioDevicePropertyDeviceUID);
    const auto name = deviceString(device, kAudioObjectPropertyName);
    if (!uid.isEmpty() && !name.isEmpty())
      result.devices.push_back({uid, name, device == defaultDevice});
  }
#elif defined(Q_OS_WIN)
  result.supported = true;
  const auto apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(apartment) && apartment != RPC_E_CHANGED_MODE) {
    result.error = QStringLiteral("无法读取系统声音设备");
    return result;
  }
  struct Apartment
  {
    bool owned;
    ~Apartment()
    {
      if (owned)
        CoUninitialize();
    }
  } cleanup{SUCCEEDED(apartment)};
  using Microsoft::WRL::ComPtr;
  ComPtr<IMMDeviceEnumerator> enumerator;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&enumerator))
      )) {
    result.error = QStringLiteral("无法读取系统声音设备");
    return result;
  }
  QString defaultId;
  ComPtr<IMMDevice> defaultDevice;
  if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &defaultDevice))) {
    LPWSTR id = nullptr;
    if (SUCCEEDED(defaultDevice->GetId(&id)) && id) {
      defaultId = QString::fromWCharArray(id);
      CoTaskMemFree(id);
    }
  }
  ComPtr<IMMDeviceCollection> devices;
  if (FAILED(enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &devices))) {
    result.error = QStringLiteral("无法读取系统声音设备");
    return result;
  }
  UINT count = 0;
  if (FAILED(devices->GetCount(&count))) {
    result.error = QStringLiteral("无法读取系统声音设备");
    return result;
  }
  for (UINT index = 0; index < count; ++index) {
    ComPtr<IMMDevice> device;
    if (FAILED(devices->Item(index, &device)))
      continue;
    LPWSTR rawId = nullptr;
    if (FAILED(device->GetId(&rawId)) || !rawId)
      continue;
    const auto id = QString::fromWCharArray(rawId);
    CoTaskMemFree(rawId);
    ComPtr<IPropertyStore> properties;
    if (FAILED(device->OpenPropertyStore(STGM_READ, &properties)))
      continue;
    PROPVARIANT name;
    PropVariantInit(&name);
    const auto status = properties->GetValue(PKEY_Device_FriendlyName, &name);
    if (SUCCEEDED(status) && name.vt == VT_LPWSTR && name.pwszVal && !id.isEmpty() && *name.pwszVal)
      result.devices.push_back({id, QString::fromWCharArray(name.pwszVal), id == defaultId});
    PropVariantClear(&name);
  }
#else
  result.error = QStringLiteral("当前平台尚不支持跨设备语音");
#endif
  std::sort(result.devices.begin(), result.devices.end(), [](const AudioInput &a, const AudioInput &b) {
    if (a.isDefault != b.isDefault)
      return a.isDefault;
    return a.name.localeAwareCompare(b.name) < 0;
  });
  return result;
}

} // namespace deskflow::gui::voice
