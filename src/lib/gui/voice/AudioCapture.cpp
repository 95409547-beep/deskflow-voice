/*
 * SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "AudioCapture.h"
#ifndef Q_OS_MACOS
namespace deskflow::gui::voice {
class UnavailableCapture final : public AudioCapture
{
public:
  using AudioCapture::AudioCapture;
  bool permitted() const override
  {
    return false;
  }
  void requestPermission(std::function<void(bool)> done) override
  {
    done(false);
  }
  bool start(const QString &, QString &error) override
  {
    error = QStringLiteral("此构建尚无本机麦克风发送端");
    return false;
  }
  void stop() override
  {
  }
};
AudioCapture *createAudioCapture(QObject *parent)
{
  return new UnavailableCapture(parent);
}
} // namespace deskflow::gui::voice
#endif
