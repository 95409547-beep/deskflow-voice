/*
 * SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <QVariantMap>

namespace deskflow::gui::voice {

// Preferences express intent, never device readiness, pairing or permission.
struct Preferences
{
  bool enabled = false;
  QString role = QStringLiteral("auto");
  QString inputUid;
  QString targetComputer;
  QString shortcut = QStringLiteral("F8");

  static Preferences defaults();
  static Preferences fromValues(const QVariantMap &values);
  QVariantMap toValues() const;
  bool operator==(const Preferences &) const = default;
};

} // namespace deskflow::gui::voice
