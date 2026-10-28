/*
 * SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "Protocol.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QUuid>
#include <QtEndian>
#include <stdexcept>

namespace deskflow::voice::control {
QString endpointName(qint64 pid)
{
  return pid > 0 ? QStringLiteral("deskflow-voice-%1").arg(pid) : QString();
}
QByteArray newSecret()
{
  QByteArray result(32, '\0');
  for (int index = 0; index < 8; ++index)
    qToBigEndian<quint32>(QRandomGenerator::system()->generate(), result.data() + index * 4);
  return result;
}
QByteArray proof(
    const QByteArray &secret, const QString &role, const QString &nonce, const QString &epoch, const QString &lease,
    qint64 pid
)
{
  const auto data =
      QJsonDocument(QJsonArray{role, nonce, epoch, lease, QString::number(pid)}).toJson(QJsonDocument::Compact);
  return QMessageAuthenticationCode::hash(data, secret, QCryptographicHash::Sha256).toHex();
}
bool equalProof(const QByteArray &left, const QByteArray &right)
{
  if (left.size() != 64 || right.size() != 64)
    return false;
  unsigned difference = 0;
  for (int index = 0; index < 64; ++index)
    difference |= static_cast<unsigned char>(left.at(index) ^ right.at(index));
  return difference == 0;
}
bool uuid(const QString &value)
{
  return !QUuid(value).isNull();
}
HoldKey parseKey(const QString &value)
{
  if (value == QStringLiteral("F8"))
    return HoldKey::F8;
  if (value == QStringLiteral("right-option"))
    return HoldKey::RightOption;
  if (value == QStringLiteral("right-control"))
    return HoldKey::RightControl;
  return HoldKey::Other;
}
QByteArray Frames::encode(const QJsonObject &value)
{
  const auto json = QJsonDocument(value).toJson(QJsonDocument::Compact);
  if (json.size() > maximum)
    throw std::runtime_error("control frame too large");
  QByteArray result(4, '\0');
  qToBigEndian<quint32>(static_cast<quint32>(json.size()), result.data());
  return result + json;
}
QVector<QJsonObject> Frames::append(const QByteArray &bytes)
{
  if (bytes.size() > maximum)
    throw std::runtime_error("control read too large");
  m_pending += bytes;
  QVector<QJsonObject> result;
  while (m_pending.size() >= 4) {
    const auto size = qFromBigEndian<quint32>(m_pending.constData());
    if (!size || size > maximum)
      throw std::runtime_error("control frame length");
    if (m_pending.size() < size + 4)
      break;
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(m_pending.mid(4, size), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
      throw std::runtime_error("control JSON");
    result.push_back(doc.object());
    m_pending.remove(0, size + 4);
  }
  if (m_pending.size() > maximum + 4)
    throw std::runtime_error("control buffer limit");
  return result;
}
} // namespace deskflow::voice::control
