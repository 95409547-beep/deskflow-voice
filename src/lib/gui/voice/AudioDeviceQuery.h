/*
 * SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <QString>
#include <QVector>

namespace deskflow::gui::voice {

struct AudioInput
{
  QString uid;
  QString name;
  bool isDefault = false;
};

struct AudioInputs
{
  bool supported = false;
  QVector<AudioInput> devices;
  QString error;
};

// Enumeration only. Never starts a stream, requests permission or changes defaults.
AudioInputs queryAudioInputs();

} // namespace deskflow::gui::voice
