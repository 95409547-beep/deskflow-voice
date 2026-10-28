/*
 * SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include <QObject>
#include <functional>

namespace deskflow::gui::voice {

// Implementations emit 20ms frames of 48kHz mono PCM16LE, only after start.
class AudioCapture : public QObject
{
  Q_OBJECT
public:
  using QObject::QObject;
  virtual bool permitted() const = 0;
  virtual void requestPermission(std::function<void(bool)> completed) = 0;
  virtual bool start(const QString &uid, QString &error) = 0;
  virtual void stop() = 0;
Q_SIGNALS:
  void pcm(const QByteArray &bytes);
  void failed(const QString &error);
};
AudioCapture *createAudioCapture(QObject *parent);
} // namespace deskflow::gui::voice
