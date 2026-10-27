/*
 * SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "VoicePreferences.h"

#include "common/Settings.h"

namespace deskflow::gui::voice {

Preferences Preferences::defaults()
{
  Preferences result;
#ifdef Q_OS_MACOS
  result.shortcut = QStringLiteral("right-option");
#endif
  return result;
}

Preferences Preferences::fromValues(const QVariantMap &values)
{
  auto result = defaults();
  result.enabled = values.value(Settings::Voice::Enabled, false).toBool();
  const auto role = values.value(Settings::Voice::Role, result.role).toString();
  if (role == QStringLiteral("auto") || role == QStringLiteral("send") || role == QStringLiteral("receive"))
    result.role = role;
#ifdef Q_OS_MACOS
  if (result.role == QStringLiteral("receive"))
    result.role = QStringLiteral("auto");
#endif
  result.inputUid = values.value(Settings::Voice::InputUid).toString();
  result.targetComputer = values.value(Settings::Voice::TargetComputer).toString();
  const auto key = values.value(Settings::Voice::Shortcut, result.shortcut).toString();
  if (key == QStringLiteral("F8") || key == QStringLiteral("right-option") || key == QStringLiteral("right-control"))
    result.shortcut = key;
  return result;
}

QVariantMap Preferences::toValues() const
{
  return {
      {Settings::Voice::Enabled, enabled},
      {Settings::Voice::Role, role},
      {Settings::Voice::InputUid, inputUid},
      {Settings::Voice::TargetComputer, targetComputer},
      {Settings::Voice::Shortcut, shortcut}
  };
}

} // namespace deskflow::gui::voice
