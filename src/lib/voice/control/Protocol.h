/*
 * SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include "voice/KeyGate.h"
#include <QJsonObject>
#include <QVector>

namespace deskflow::voice::control {
QString endpointName(qint64 pid);
QByteArray newSecret();
QByteArray proof(
    const QByteArray &secret, const QString &role, const QString &nonce, const QString &epoch, const QString &lease,
    qint64 pid
);
bool equalProof(const QByteArray &left, const QByteArray &right);
bool uuid(const QString &value);
HoldKey parseKey(const QString &value);
class Frames
{
public:
  static constexpr int maximum = 8192;
  static QByteArray encode(const QJsonObject &value);
  QVector<QJsonObject> append(const QByteArray &bytes);
  void clear()
  {
    m_pending.clear();
  }

private:
  QByteArray m_pending;
};
} // namespace deskflow::voice::control
