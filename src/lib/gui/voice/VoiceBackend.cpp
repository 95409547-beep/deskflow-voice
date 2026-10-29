/*
 * SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "VoiceBackend.h"
#include "AudioCapture.h"
#include <QCryptographicHash>
#include <QPointer>
#include <QSslCertificate>
#include <QUuid>
#include <utility>

namespace deskflow::gui::voice {
using deskflow::voice::EffectKind;
using deskflow::voice::Phase;

VoiceBackend::VoiceBackend(QObject *parent, AudioCapture *capture, deskflow::voice::Platform sender)
    : QObject(parent),
      m_capture(capture ? capture : createAudioCapture(this)),
      m_session(sender)
{
  if (capture)
    capture->setParent(this);
  m_socket.setPeerVerifyMode(QSslSocket::VerifyNone); // Exact DER pin is checked before sending the token.
  m_socket.setProtocol(QSsl::TlsV1_2OrLater);
  m_socket.setReadBufferSize(131080);
  m_deadline.setSingleShot(true);
  m_heartbeat.setInterval(3000);
  connect(&m_socket, &QSslSocket::encrypted, this, [this] {
    if (m_socket.peerCertificate().isNull() ||
        m_socket.peerCertificate().digest(QCryptographicHash::Sha256) != m_peer.certificateSha256) {
      fail(tr("接收端身份不匹配"));
      return;
    }
    send(
        {{QStringLiteral("type"), QStringLiteral("hello")},
         {QStringLiteral("protocol"), 2},
         {QStringLiteral("token"), m_peer.token}}
    );
  });
  connect(&m_socket, &QSslSocket::readyRead, this, &VoiceBackend::read);
  connect(&m_socket, &QSslSocket::errorOccurred, this, [this] {
    if (!m_closing)
      fail(tr("语音连接中断，请重新连接"));
  });
  connect(&m_socket, &QSslSocket::disconnected, this, [this] {
    if (!m_closing)
      fail(tr("语音连接已断开"));
  });
  connect(&m_deadline, &QTimer::timeout, this, [this] { fail(tr("语音启动、结束或说话时长超时，已停止收音")); });
  connect(&m_heartbeat, &QTimer::timeout, this, [this] {
    if (++m_silentHeartbeats > 3) {
      fail(tr("接收端未响应"));
      return;
    }
    send({{QStringLiteral("type"), QStringLiteral("ping")}, {QStringLiteral("epoch"), m_epoch}});
  });
  connect(m_capture, &AudioCapture::pcm, this, &VoiceBackend::audio);
  connect(m_capture, &AudioCapture::failed, this, &VoiceBackend::fail);
  setStatus(tr("尚未连接语音接收端"));
}
VoiceBackend::~VoiceBackend()
{
  close();
}
bool VoiceBackend::busy() const
{
  return m_session.phase() != Phase::Idle && m_session.phase() != Phase::ReleaseRequired;
}
void VoiceBackend::setStatus(const QString &value)
{
  m_status = value;
  Q_EMIT changed();
}
void VoiceBackend::configure(const VoicePeer &peer)
{
  close();
  m_peer = peer;
}

void VoiceBackend::unavailable(const QString &reason)
{
  fail(reason);
}
void VoiceBackend::controlAvailable(bool available)
{
  if (m_controlAvailable == available)
    return;
  m_controlAvailable = available;
  if (!available)
    controlRoute({}, 0, false);
  Q_EMIT changed();
}
void VoiceBackend::requireScreenRouting(bool required)
{
  m_routingRequired = required;
  updateRoute();
}
void VoiceBackend::controlRoute(const QString &screen, quint64 generation, bool eligible)
{
  if (m_controlScreen == screen && m_routeGeneration == generation && m_routeEligible == eligible)
    return;
  m_controlScreen = screen;
  m_routeGeneration = generation;
  m_routeEligible = eligible;
  updateRoute();
  Q_EMIT changed();
}
void VoiceBackend::updateRoute()
{
  const bool allowed = m_ready && routeEligible();
  const auto generation = m_routingRequired ? m_routeGeneration : 1;
  effects(m_session.routeChanged(
      {m_peer.host.toStdString(), generation, deskflow::voice::Platform::Windows, m_ready, allowed}
  ));
}
void VoiceBackend::setInputUid(const QString &uid)
{
  if (uid == m_uid)
    return;
  if (busy()) {
    fail(tr("声音来源已更改，请重新连接后再试"));
  }
  m_uid = uid;
}
void VoiceBackend::connectPeer()
{
  close();
  if (m_peer.host.isEmpty() || !m_peer.port || m_peer.certificateSha256.size() != 32 || m_peer.token.size() != 64) {
    setStatus(tr("请先准备接收端配对信息"));
    return;
  }
  m_closing = false;
  setStatus(tr("正在连接语音接收端"));
  m_socket.connectToHostEncrypted(m_peer.host, m_peer.port);
  m_deadline.start(7000);
}
void VoiceBackend::close()
{
  m_closing = true;
  m_capture->stop();
  m_preroll.clear();
  m_ready = false;
  m_deadline.stop();
  m_heartbeat.stop();
  m_session.connectionLost();
  m_socket.abort();
  m_wire.clear();
  m_completed.clear();
  m_completedOrder.clear();
  m_epoch.clear();
  m_round.clear();
  m_closing = false;
  setStatus(tr("语音已停用"));
}
void VoiceBackend::fail(const QString &error)
{
  close();
  setStatus(error);
}
void VoiceBackend::send(QJsonObject object)
{
  if (!m_socket.isEncrypted())
    return;
  const auto bytes = VoiceWire::encode(object);
  if (m_socket.bytesToWrite() + bytes.size() > 131072 || m_socket.write(bytes) != bytes.size())
    fail(tr("网络发送积压，已停止本轮"));
}
QJsonObject VoiceBackend::roundMessage(const QString &type) const
{
  return {{QStringLiteral("type"), type}, {QStringLiteral("epoch"), m_epoch}, {QStringLiteral("session"), m_round}};
}
void VoiceBackend::press()
{
  if (!m_ready || m_permissionPending)
    return;
  if (m_routingRequired && (!m_routeEligible || m_peer.screen.isEmpty() || m_peer.screen != m_controlScreen))
    return;
  if (!m_capture->permitted()) {
    m_permissionPending = true;
    setStatus(tr("请允许麦克风访问，授权后重新按住试说"));
    const QPointer<VoiceBackend> self(this);
    m_capture->requestPermission([self](bool granted) {
      if (!self)
        return;
      self->m_permissionPending = false;
      self->setStatus(granted ? tr("麦克风已允许，请重新按住试说") : tr("麦克风权限未允许"));
    });
    return;
  }
  if (m_session.phase() == Phase::Idle && !m_session.pressed()) {
    m_round = QUuid::createUuid().toString(QUuid::WithoutBraces);
    ++m_sequence;
  }
  effects(m_session.press(m_sequence));
}
void VoiceBackend::release()
{
  effects(m_session.release());
  Q_EMIT changed();
}
void VoiceBackend::effects(const std::vector<deskflow::voice::Effect> &values)
{
  for (const auto &effect : values) {
    if (effect.kind == EffectKind::StartCapture) {
      QString error;
      if (!m_capture->start(m_uid, error)) {
        fail(error);
        return;
      }
      m_preroll.clear();
      setStatus(tr("正在启动 Windows 语音"));
    } else if (effect.kind == EffectKind::Begin) {
      auto request = roundMessage(QStringLiteral("begin"));
      request.insert(QStringLiteral("sequence"), QString::number(effect.session));
      request.insert(QStringLiteral("generation"), QString::number(effect.target.generation));
      send(request);
      if (!m_ready)
        return;
      m_deadline.start(5000);
    } else if (effect.kind == EffectKind::ForwardAudio) {
      setStatus(tr("正在向 Windows 说话"));
      const auto pending = std::exchange(m_preroll, {});
      for (qsizetype index = 0; index < pending.size() && m_ready; index += 1920)
        audio(pending.mid(index, 1920));
      if (!m_ready)
        return;
      m_deadline.start(60000);
    } else if (effect.kind == EffectKind::StopCapture) {
      m_capture->stop();
      m_preroll.clear();
    } else if (effect.kind == EffectKind::End) {
      send(roundMessage(QStringLiteral("end")));
      if (!m_ready)
        return;
      setStatus(tr("正在结束本句"));
      m_deadline.start(5000);
    }
  }
}
void VoiceBackend::audio(const QByteArray &bytes)
{
  if (m_session.phase() == Phase::Starting) {
    m_preroll += bytes;
    if (m_preroll.size() > 24000)
      m_preroll.remove(0, m_preroll.size() - 24000);
  } else if (m_session.phase() == Phase::Recording) {
    if (bytes.isEmpty() || (bytes.size() & 1) || bytes.size() > 19200) {
      fail(tr("麦克风音频格式无效"));
      return;
    }
    auto frame = roundMessage(QStringLiteral("audio"));
    frame.insert(QStringLiteral("pcm"), QString::fromLatin1(bytes.toBase64()));
    send(frame);
  }
}
void VoiceBackend::read()
{
  try {
    while (m_socket.bytesAvailable()) {
      const auto frames = m_wire.append(m_socket.read(4096));
      for (const auto &frame : frames) {
        message(frame);
        if (!m_socket.isEncrypted())
          return;
      }
    }
  } catch (const std::exception &) {
    fail(tr("接收端协议无效"));
  }
}
void VoiceBackend::message(const QJsonObject &object)
{
  const auto type = object.value(QStringLiteral("type")).toString();
  if (type == QStringLiteral("hello") && m_epoch.isEmpty()) {
    const auto epoch = object.value(QStringLiteral("epoch")).toString();
    if (object.value(QStringLiteral("protocol")).toInt() != 2 || QUuid(epoch).isNull() ||
        object.value(QStringLiteral("receiver")).toString() != QStringLiteral("windows") ||
        object.value(QStringLiteral("ready")).toBool() != true ||
        object.value(QStringLiteral("sample_rate")).toInt() != 48000 ||
        object.value(QStringLiteral("channels")).toInt() != 1 ||
        object.value(QStringLiteral("format")).toString() != QStringLiteral("s16le")) {
      fail(tr("接收端版本或就绪状态不兼容"));
      return;
    }
    m_epoch = epoch;
    m_ready = true;
    m_deadline.stop();
    m_silentHeartbeats = 0;
    m_heartbeat.start();
    updateRoute();
    setStatus(tr("Windows 接收端已就绪"));
    return;
  }
  if (!m_ready || object.value(QStringLiteral("epoch")).toString() != m_epoch) {
    fail(tr("接收端连接代次不匹配"));
    return;
  }
  m_silentHeartbeats = 0;
  if (type == QStringLiteral("pong"))
    return;
  if (type == QStringLiteral("error")) {
    fail(tr("接收端拒绝本轮，请检查 Windows 状态"));
    return;
  }
  const auto id = object.value(QStringLiteral("session")).toString();
  // A receiver cancellation can cross our queued END. Its repeated completion
  // or a late START must not disconnect or revive an already completed round.
  if ((type == QStringLiteral("started") || type == QStringLiteral("ended")) && m_completed.contains(id))
    return;
  if (id != m_round || m_round.isEmpty()) {
    fail(tr("接收端会话不匹配"));
    return;
  }
  if (type == QStringLiteral("started")) {
    if (m_session.phase() == Phase::Starting)
      effects(m_session.receiverStarted(m_sequence));
  } else if (type == QStringLiteral("ended")) {
    effects(m_session.receiverEnded(m_sequence));
    m_deadline.stop();
    m_completed.insert(m_round);
    m_completedOrder.append(m_round);
    if (m_completedOrder.size() > 64)
      m_completed.remove(m_completedOrder.takeFirst());
    m_round.clear();
    if (!object.value(QStringLiteral("ready")).toBool()) {
      fail(tr("Windows 尚未完成收尾，请重新连接"));
      return;
    }
    setStatus(tr("已结束，可以继续下一句"));
    Q_EMIT roundEnded(
        static_cast<quint64>(object.value(QStringLiteral("audio_bytes")).toDouble()),
        object.value(QStringLiteral("reason")).toString()
    );
  } else {
    fail(tr("接收端消息不兼容"));
  }
}
} // namespace deskflow::gui::voice
