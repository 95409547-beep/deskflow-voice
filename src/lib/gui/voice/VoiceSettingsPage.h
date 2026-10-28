/*
 * SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "AudioDeviceQuery.h"
#include "VoicePreferences.h"

#include <QPointer>
#include <QWidget>
#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTabWidget;

namespace deskflow::gui::voice {
class VoiceBackend;

struct ConnectionSnapshot
{
  bool ipcAvailable = false;
  bool controlConnected = false;
  bool server = true;
  QStringList peerNames;
  bool operator==(const ConnectionSnapshot &) const = default;
};

class SettingsPage : public QWidget
{
  Q_OBJECT
public:
  explicit SettingsPage(QWidget *parent = nullptr, std::function<AudioInputs()> query = queryAudioInputs);
  void loadPreferences(const Preferences &preferences);
  Preferences preferences() const;
  void resetToDefaults();
  bool isModified() const;
  bool isDefault() const;
  void setWritable(bool writable);
  void setConnectionSnapshot(const ConnectionSnapshot &snapshot);
  bool serviceReady() const;
  void bindBackend(VoiceBackend *backend);

Q_SIGNALS:
  void preferencesChanged();

private:
  bool sending() const;
  bool sourcePresent() const;
  void refreshDevices();
  void refreshStatus();
  void refreshPeers();

  std::function<AudioInputs()> m_query;
  AudioInputs m_inputs;
  Preferences m_baseline;
  ConnectionSnapshot m_connection;
  bool m_loading = false;
  bool m_writable = true;
  QTabWidget *m_tabs;
  QCheckBox *m_enabled;
  QComboBox *m_role;
  QComboBox *m_input;
  QComboBox *m_target;
  QComboBox *m_shortcut;
  QLabel *m_title;
  QLabel *m_hint;
  QLabel *m_connectionLabel;
  QLabel *m_sourceLabel;
  QPushButton *m_action;
  QPushButton *m_talk;
  QLabel *m_serviceLabel;
  QPointer<VoiceBackend> m_backend;
};

} // namespace deskflow::gui::voice
