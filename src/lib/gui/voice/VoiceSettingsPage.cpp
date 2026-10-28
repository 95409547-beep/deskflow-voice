/*
 * SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "VoiceSettingsPage.h"
#include "VoiceBackend.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

namespace deskflow::gui::voice {

SettingsPage::SettingsPage(QWidget *parent, std::function<AudioInputs()> query)
    : QWidget(parent),
      m_query(std::move(query)),
      m_tabs(new QTabWidget(this)),
      m_enabled(new QCheckBox(tr("启用"), this)),
      m_role(new QComboBox(this)),
      m_input(new QComboBox(this)),
      m_target(new QComboBox(this)),
      m_shortcut(new QComboBox(this)),
      m_title(new QLabel(this)),
      m_hint(new QLabel(this)),
      m_connectionLabel(new QLabel(this)),
      m_sourceLabel(new QLabel(this)),
      m_action(new QPushButton(this)),
      m_talk(new QPushButton(tr("按住试说"), this)),
      m_serviceLabel(new QLabel(this))
{
  auto *root = new QVBoxLayout(this);
  auto *heading = new QHBoxLayout;
  auto *name = new QLabel(tr("跨设备语音"), this);
  auto font = name->font();
  font.setPointSize(font.pointSize() + 3);
  name->setFont(font);
  heading->addWidget(name);
  heading->addStretch();
  heading->addWidget(m_enabled);
  root->addLayout(heading);
  root->addWidget(m_tabs);
  m_tabs->setObjectName(QStringLiteral("voicePages"));
  m_enabled->setObjectName(QStringLiteral("voiceEnabled"));
  m_role->setObjectName(QStringLiteral("voiceRole"));
  m_input->setObjectName(QStringLiteral("voiceInput"));
  m_target->setObjectName(QStringLiteral("voiceTarget"));
  m_shortcut->setObjectName(QStringLiteral("voiceShortcut"));
  m_title->setObjectName(QStringLiteral("voiceStatus"));
  m_connectionLabel->setObjectName(QStringLiteral("voiceControlConnection"));
  m_action->setObjectName(QStringLiteral("voicePrimaryAction"));
  m_talk->setObjectName(QStringLiteral("voiceTalk"));
  m_talk->setMinimumWidth(132);
  m_talk->setAutoDefault(false);

  auto *home = new QWidget(this);
  auto *homeLayout = new QVBoxLayout(home);
  homeLayout->setContentsMargins(24, 24, 24, 24);
  homeLayout->addStretch();
  m_title->setAlignment(Qt::AlignCenter);
  auto titleFont = m_title->font();
  titleFont.setPointSize(titleFont.pointSize() + 4);
  m_title->setFont(titleFont);
  m_hint->setAlignment(Qt::AlignCenter);
  m_hint->setWordWrap(true);
  homeLayout->addWidget(m_title);
  homeLayout->addSpacing(12);
  homeLayout->addWidget(m_hint);
  homeLayout->addSpacing(16);
  auto *actionRow = new QHBoxLayout;
  actionRow->addStretch();
  m_action->setMinimumWidth(132);
  actionRow->addWidget(m_action);
  actionRow->addWidget(m_talk);
  actionRow->addStretch();
  homeLayout->addLayout(actionRow);
  homeLayout->addStretch();
  m_tabs->addTab(home, tr("使用"));

  auto *devices = new QWidget(this);
  auto *deviceLayout = new QVBoxLayout(devices);
  auto *deviceForm = new QFormLayout;
  m_role->addItem(tr("自动选择"), QStringLiteral("auto"));
  m_role->addItem(tr("发送声音"), QStringLiteral("send"));
  m_role->addItem(tr("接收语音"), QStringLiteral("receive"));
#ifdef Q_OS_MACOS
  if (auto *model = qobject_cast<QStandardItemModel *>(m_role->model()))
    model->item(2)->setEnabled(false);
#endif
  deviceForm->addRow(tr("本机用途"), m_role);
  m_connectionLabel->setWordWrap(true);
  deviceForm->addRow(tr("键鼠连接"), m_connectionLabel);
  deviceForm->addRow(tr("目标电脑"), m_target);
  deviceForm->addRow(tr("声音来源"), m_input);
  m_sourceLabel->setWordWrap(true);
  deviceForm->addRow(QString(), m_sourceLabel);
  m_serviceLabel->setWordWrap(true);
  deviceForm->addRow(tr("语音检测"), m_serviceLabel);
  deviceLayout->addLayout(deviceForm);
  auto *refresh = new QPushButton(tr("重新检测设备"), this);
  refresh->setObjectName(QStringLiteral("voiceRefreshDevices"));
  deviceLayout->addWidget(refresh, 0, Qt::AlignLeft);
  deviceLayout->addStretch();
  m_tabs->addTab(devices, tr("设备"));

  auto *settings = new QWidget(this);
  auto *settingsLayout = new QVBoxLayout(settings);
  auto *settingsForm = new QFormLayout;
#ifdef Q_OS_MACOS
  m_shortcut->addItem(tr("右 Option"), QStringLiteral("right-option"));
#endif
  m_shortcut->addItem(QStringLiteral("F8"), QStringLiteral("F8"));
#ifdef Q_OS_WIN
  m_shortcut->addItem(tr("右 Ctrl"), QStringLiteral("right-control"));
#endif
  settingsForm->addRow(tr("按住说话键"), m_shortcut);
  m_startup = new QLabel(tr("登录准备将在语音后台接入后提供"), this);
  m_startup->setWordWrap(true);
  settingsForm->addRow(tr("自动准备"), m_startup);
  settingsLayout->addLayout(settingsForm);
  auto *details = new QGroupBox(tr("开发状态"), this);
  details->setCheckable(true);
  details->setChecked(false);
  auto *detailsLayout = new QVBoxLayout(details);
  m_detailsText = new QLabel(tr("当前仅枚举系统声音设备并读取核心 IPC 状态，不录音、发键或启动语音。"), details);
  m_detailsText->setWordWrap(true);
  m_detailsText->setVisible(false);
  detailsLayout->addWidget(m_detailsText);
  connect(details, &QGroupBox::toggled, m_detailsText, &QLabel::setVisible);
  settingsLayout->addWidget(details);
  settingsLayout->addStretch();
  m_tabs->addTab(settings, tr("设置"));

  const auto changed = [this] {
    if (m_loading)
      return;
    refreshStatus();
    Q_EMIT preferencesChanged();
  };
  connect(m_enabled, &QCheckBox::toggled, this, changed);
  connect(m_input, &QComboBox::currentIndexChanged, this, changed);
  connect(m_input, &QComboBox::currentIndexChanged, this, [this] {
    if (!m_loading && m_liveControls && m_backend)
      m_backend->setInputUid(m_input->currentData().toString());
  });
  connect(m_enabled, &QCheckBox::toggled, this, [this](bool enabled) {
    if (!m_loading && m_liveControls && !enabled && m_backend)
      m_backend->close();
  });
  connect(m_target, &QComboBox::currentIndexChanged, this, changed);
  connect(m_shortcut, &QComboBox::currentIndexChanged, this, changed);
  connect(m_role, &QComboBox::currentIndexChanged, this, [this, changed] {
    if (!m_loading) {
      refreshDevices();
      changed();
    }
  });
  connect(refresh, &QPushButton::clicked, this, &SettingsPage::refreshDevices);
  connect(m_action, &QPushButton::clicked, this, [this] {
    if (!m_enabled->isChecked() && m_writable)
      m_enabled->setChecked(true);
    if (m_backend && m_enabled->isChecked()) {
      if (m_liveControls)
        m_backend->connectPeer();
      else
        Q_EMIT reconnectRequested();
    } else
      m_tabs->setCurrentIndex(1);
  });
  connect(m_talk, &QPushButton::pressed, this, [this] {
    if (m_backend)
      m_backend->press();
  });
  connect(m_talk, &QPushButton::released, this, [this] {
    if (m_backend)
      m_backend->release();
  });
  loadPreferences(Preferences::defaults());
}

void SettingsPage::loadPreferences(const Preferences &preferences)
{
  m_loading = true;
  m_baseline = preferences;
  m_enabled->setChecked(preferences.enabled);
  const auto role = m_role->findData(preferences.role);
  m_role->setCurrentIndex(role >= 0 ? role : 0);
#ifdef Q_OS_MACOS
  if (m_role->currentData() == QStringLiteral("receive"))
    m_role->setCurrentIndex(0);
#endif
  const auto shortcut = m_shortcut->findData(preferences.shortcut);
  m_shortcut->setCurrentIndex(shortcut >= 0 ? shortcut : 0);
  m_baseline.role = m_role->currentData().toString();
  m_baseline.shortcut = m_shortcut->currentData().toString();
  // Keep preferences separate from discovery, including temporarily missing IDs.
  m_input->clear();
  m_input->addItem(QString(), preferences.inputUid);
  m_target->clear();
  m_target->addItem(QString(), preferences.targetComputer);
  m_loading = false;
  refreshPeers();
  refreshDevices();
}

Preferences SettingsPage::preferences() const
{
  return {
      m_enabled->isChecked(), m_role->currentData().toString(), m_input->currentData().toString(),
      m_target->currentData().toString(), m_shortcut->currentData().toString()
  };
}

void SettingsPage::resetToDefaults()
{
  const auto baseline = m_baseline;
  loadPreferences(Preferences::defaults());
  m_baseline = baseline;
  Q_EMIT preferencesChanged();
}

bool SettingsPage::isModified() const
{
  return preferences() != m_baseline;
}

bool SettingsPage::isDefault() const
{
  return preferences() == Preferences::defaults();
}

void SettingsPage::setWritable(bool writable)
{
  m_writable = writable;
  refreshStatus();
}

bool SettingsPage::sending() const
{
#ifdef Q_OS_MACOS
  return true;
#else
  const auto role = m_role->currentData().toString();
  return role == QStringLiteral("send") || (role == QStringLiteral("auto") && m_connection.server);
#endif
}

void SettingsPage::setConnectionSnapshot(const ConnectionSnapshot &snapshot)
{
  if (m_connection == snapshot)
    return;
  const bool roleChanged = m_connection.server != snapshot.server;
  m_connection = snapshot;
  refreshPeers();
  if (roleChanged)
    refreshDevices();
  refreshStatus();
}

void SettingsPage::refreshPeers()
{
  const QSignalBlocker blocker(m_target);
  const auto selected = m_target->currentData().toString();
  m_target->clear();
  m_target->addItem(m_backend ? tr("已配对的 Windows") : tr("跟随当前控制位置"), QString());
  if (m_connection.ipcAvailable && m_connection.controlConnected) {
    for (const auto &peer : m_connection.peerNames) {
      if (!peer.isEmpty() && m_target->findData(peer) < 0)
        m_target->addItem(peer, peer);
    }
  }
  if (!selected.isEmpty() && m_target->findData(selected) < 0)
    m_target->addItem(tr("%1（未连接）").arg(selected), selected);
  m_target->setCurrentIndex(qMax(0, m_target->findData(selected)));
}

void SettingsPage::refreshDevices()
{
  const QSignalBlocker blocker(m_input);
  const auto selected = m_input->currentData().toString();
  m_inputs = sending() ? m_query() : AudioInputs{};
  m_input->clear();
  const auto defaultDevice =
      std::find_if(m_inputs.devices.cbegin(), m_inputs.devices.cend(), [](const AudioInput &device) {
        return device.isDefault;
      });
  m_input->addItem(
      defaultDevice == m_inputs.devices.cend() ? tr("系统默认麦克风") : tr("系统默认（%1）").arg(defaultDevice->name),
      QString()
  );
  for (const auto &device : m_inputs.devices)
    m_input->addItem(device.isDefault ? tr("%1（系统默认）").arg(device.name) : device.name, device.uid);
  if (!selected.isEmpty() && m_input->findData(selected) < 0)
    m_input->addItem(tr("已选设备不可用"), selected);
  m_input->setCurrentIndex(qMax(0, m_input->findData(selected)));
  refreshStatus();
}

bool SettingsPage::sourcePresent() const
{
  const auto selected = m_input->currentData().toString();
  return std::any_of(m_inputs.devices.begin(), m_inputs.devices.end(), [&selected](const AudioInput &device) {
    return selected.isEmpty() ? device.isDefault : device.uid == selected;
  });
}

bool SettingsPage::serviceReady() const
{
  return m_backend && m_backend->inputReady();
}

void SettingsPage::bindBackend(VoiceBackend *backend, bool liveControls)
{
  if (m_backend)
    disconnect(m_backend, nullptr, this, nullptr);
  m_backend = backend;
  m_liveControls = liveControls;
  refreshPeers();
  if (backend) {
    m_startup->setText(
        backend->routed() ? tr("随本软件准备；开机启动将在安装器中提供。") : tr("当前使用独立测试窗口。")
    );
    m_detailsText->setText(tr("声音发送到配对电脑。按住时收音，松开后停止。"));
    if (liveControls)
      backend->setInputUid(m_input->currentData().toString());
    connect(backend, &VoiceBackend::changed, this, &SettingsPage::refreshStatus);
    connect(backend, &QObject::destroyed, this, &SettingsPage::refreshStatus);
  }
  refreshStatus();
}

void SettingsPage::refreshStatus()
{
  const bool sender = sending();
  m_role->setEnabled(m_writable);
  m_enabled->setEnabled(m_writable);
  m_input->setEnabled(m_writable && sender);
  m_target->setEnabled(m_writable && sender && !m_backend);
  m_shortcut->setEnabled(m_writable && sender);
  m_connectionLabel->setText(
      !m_connection.ipcAvailable      ? tr("等待核心 IPC 状态")
      : m_connection.controlConnected ? tr("键鼠已连接")
                                      : tr("尚未连接电脑")
  );
  m_sourceLabel->setText(
      !sender                     ? tr("声音来源在发送端设置")
      : !m_inputs.error.isEmpty() ? m_inputs.error
      : sourcePresent()           ? tr("已找到声音来源，尚未收音")
                                  : tr("声音来源不可用，请连接或更换设备")
  );
#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN)
  m_title->setText(tr("当前平台尚不支持语音"));
  m_enabled->setEnabled(false);
#else
  m_title->setText(m_enabled->isChecked() ? tr("语音服务尚未就绪") : tr("开启跨设备语音"));
#endif
  m_hint->setText(m_enabled->isChecked() ? tr("此开发构建尚未接入语音后台。") : tr("先检查电脑连接与声音来源。"));
  m_action->setText(m_enabled->isChecked() ? tr("检查设备") : tr("开始使用"));
  m_talk->setVisible(m_backend && !m_backend->routed() && serviceReady() && m_enabled->isChecked());
  m_talk->setEnabled(m_writable && sender && sourcePresent());
  m_action->setVisible(!m_backend || !serviceReady() || !m_enabled->isChecked());
  m_serviceLabel->setText(serviceReady() ? tr("接收端已就绪") : tr("等待接收端"));
  if (m_backend && m_enabled->isChecked()) {
    m_title->setText(m_backend->status());
    m_hint->setText(
        m_backend->routed()
            ? tr("%1 · 到 Windows 按住 %2 说话。").arg(m_input->currentText(), m_shortcut->currentText())
            : tr("%1 · 按住试说，松开结束。").arg(m_input->currentText())
    );
    m_action->setText(tr("连接语音"));
  }
}

} // namespace deskflow::gui::voice
