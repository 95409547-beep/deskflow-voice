/*
 * SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include <QJsonObject>
#include <QVector>

namespace deskflow::gui::voice {
class VoiceWire
{
public:
  static constexpr int maxFrame = 65536;
  static QByteArray encode(const QJsonObject &object);
  QVector<QJsonObject> append(const QByteArray &bytes);
  void clear()
  {
    m_pending.clear();
  }

private:
  QByteArray m_pending;
};
struct VoicePeer
{
  QString host;
  quint16 port = 0;
  QByteArray certificateSha256;
  QString token;
  QString screen = {};
  static VoicePeer load(const QString &file);
};
} // namespace deskflow::gui::voice
