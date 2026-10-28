/*
 * SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "VoiceWire.h"
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QtEndian>
#include <stdexcept>

namespace deskflow::gui::voice {
QByteArray VoiceWire::encode(const QJsonObject &object)
{
  const auto json = QJsonDocument(object).toJson(QJsonDocument::Compact);
  if (json.isEmpty() || json.size() > maxFrame)
    throw std::runtime_error("invalid outgoing frame");
  QByteArray result(4, '\0');
  qToBigEndian<quint32>(static_cast<quint32>(json.size()), result.data());
  result += json;
  return result;
}
QVector<QJsonObject> VoiceWire::append(const QByteArray &bytes)
{
  if (bytes.size() > maxFrame)
    throw std::runtime_error("read chunk exceeds limit");
  m_pending += bytes;
  QVector<QJsonObject> result;
  while (m_pending.size() >= 4) {
    const auto size = qFromBigEndian<quint32>(m_pending.constData());
    if (!size || size > maxFrame)
      throw std::runtime_error("invalid frame length");
    if (m_pending.size() < size + 4)
      break;
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(m_pending.mid(4, size), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
      throw std::runtime_error("invalid frame JSON");
    result.push_back(doc.object());
    m_pending.remove(0, size + 4);
  }
  if (m_pending.size() > maxFrame + 4)
    throw std::runtime_error("frame buffer exceeded limit");
  return result;
}
VoicePeer VoicePeer::load(const QString &file)
{
  QFile source(file);
  if (!source.open(QIODevice::ReadOnly) || source.size() > 16384)
    throw std::runtime_error("cannot read private peer file");
  QJsonParseError error;
  const auto doc = QJsonDocument::fromJson(source.readAll(), &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject())
    throw std::runtime_error("invalid private peer JSON");
  const auto object = doc.object();
  const auto host = object.value(QStringLiteral("host")).toString();
  const auto port = object.value(QStringLiteral("port")).toInt();
  const auto pin = object.value(QStringLiteral("certificate_sha256")).toString().remove(QLatin1Char(':')).toLatin1();
  const auto token = object.value(QStringLiteral("token")).toString();
  const QRegularExpression hex(QStringLiteral("\\A[0-9a-fA-F]{64}\\z"));
  if (host.isEmpty() || host.size() > 253 || port < 1 || port > 65535 ||
      !hex.match(QString::fromLatin1(pin)).hasMatch() || !hex.match(token).hasMatch())
    throw std::runtime_error("invalid private peer values");
  return {host, static_cast<quint16>(port), QByteArray::fromHex(pin), token};
}
} // namespace deskflow::gui::voice
