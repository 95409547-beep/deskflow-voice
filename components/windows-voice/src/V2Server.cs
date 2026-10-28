// SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
using System.Diagnostics;
using System.Net;
using System.Net.Security;
using System.Net.Sockets;
using System.Security.Authentication;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace DeskflowVoice.Windows;

internal sealed class V2Options
{
    public string BindAddress { get; set; } = "127.0.0.1";
    public int Port { get; set; }
    public string AllowedPeer { get; set; } = "127.0.0.1";
    public string TokenEnvironment { get; set; } = "DESKFLOW_VOICE_V2_TOKEN";
    public string CertificatePath { get; set; } = "";
    public string? CertificatePasswordEnvironment { get; set; }
    public string CertificateSha256 { get; set; } = "";
    public string OutputDevicePrefix { get; set; } = "";
    public string StateDirectory { get; set; } = "";
    public string? LegacyReceiverStatus { get; set; }
    public string? LegacyControllerStatus { get; set; }
    public InputMethodOptions InputMethod { get; set; } = new();
    [JsonIgnore] internal string Token { get; private set; } = "";
    [JsonIgnore] internal string? CertificatePassword { get; private set; }

    internal static V2Options Load(string file)
    {
        var path = Path.GetFullPath(file); var directory = Path.GetDirectoryName(path)!;
        using var document = JsonDocument.Parse(File.ReadAllText(path));
        if (document.RootElement.TryGetProperty("token", out _) || document.RootElement.TryGetProperty("certificatePassword", out _))
            throw new InvalidDataException("Private values must use environment references");
        var options = document.Deserialize<V2Options>(new JsonSerializerOptions { PropertyNameCaseInsensitive = true }) ?? throw new InvalidDataException("Empty V2 options");
        options.Token = Environment.GetEnvironmentVariable(options.TokenEnvironment) ?? "";
        options.CertificatePassword = options.CertificatePasswordEnvironment is null ? null : Environment.GetEnvironmentVariable(options.CertificatePasswordEnvironment);
        if (!Configuration.IsSha256(options.Token) || !Configuration.IsSha256(options.CertificateSha256) ||
            !IPAddress.TryParse(options.BindAddress, out _) || !IPAddress.TryParse(options.AllowedPeer, out _) || options.Port is < 1 or > 65535 ||
            string.IsNullOrWhiteSpace(options.OutputDevicePrefix) || string.IsNullOrWhiteSpace(options.CertificatePath) || string.IsNullOrWhiteSpace(options.StateDirectory))
            throw new InvalidDataException("Incomplete V2 endpoint, certificate, token or output configuration");
        options.InputMethod.Validate();
        options.CertificatePath = Configuration.ResolvePath(options.CertificatePath, directory);
        options.StateDirectory = Configuration.ResolvePath(options.StateDirectory, directory);
        if (options.LegacyReceiverStatus is not null) options.LegacyReceiverStatus = Configuration.ResolvePath(options.LegacyReceiverStatus, directory);
        if (options.LegacyControllerStatus is not null) options.LegacyControllerStatus = Configuration.ResolvePath(options.LegacyControllerStatus, directory);
        return options;
    }
}

[System.Runtime.Versioning.SupportedOSPlatform("windows")]
internal static class V2Server
{
    internal static int Run(V2Options options)
    {
        if (!Environment.UserInteractive || Process.GetCurrentProcess().SessionId == 0) throw new InvalidOperationException("V2 input backend requires an interactive user session");
        Directory.CreateDirectory(options.StateDirectory);
        void Log(object value)
        {
            var line = JsonSerializer.Serialize(new { utc = DateTime.UtcNow.ToString("o"), details = value });
            File.AppendAllText(Path.Combine(options.StateDirectory, "events.jsonl"), line + Environment.NewLine);
            Console.WriteLine(line);
        }
        // Match the working Windows receiver: Schannel cannot use this PFX's
        // ephemeral private-key handle. UserKeySet imports no certificate store entry.
        using var certificate = X509CertificateLoader.LoadPkcs12FromFile(options.CertificatePath, options.CertificatePassword, X509KeyStorageFlags.UserKeySet);
        if (!certificate.HasPrivateKey) throw new InvalidDataException("Server certificate has no private key");
        if (!string.Equals(certificate.GetCertHashString(HashAlgorithmName.SHA256), options.CertificateSha256, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("Server certificate fingerprint mismatch");
        using var cancellation = new CancellationTokenSource();
        ConsoleCancelEventHandler handler = (_, args) => { args.Cancel = true; cancellation.Cancel(); };
        Console.CancelKeyPress += handler;
        var listener = new TcpListener(IPAddress.Parse(options.BindAddress), options.Port);
        listener.Start(2);
        void Status(bool running, string? epoch, bool connected)
        {
            var json = JsonSerializer.Serialize(new { utc = DateTime.UtcNow.ToString("o"), running, protocol = 2, pid = Environment.ProcessId,
                bind_address = options.BindAddress, port = options.Port, connected, epoch, native_input_on_startup = false });
            var path = Path.Combine(options.StateDirectory, "status.json"); File.WriteAllText(path + ".tmp", json); File.Move(path + ".tmp", path, true);
        }
        Status(true, null, false); Log(new { type = "listening", protocol = 2, options.Port, NativeActions = 0 });
        try
        {
            while (!cancellation.IsCancellationRequested)
            {
                TcpClient client;
                try { client = listener.AcceptTcpClientAsync(cancellation.Token).AsTask().GetAwaiter().GetResult(); }
                catch (OperationCanceledException) { break; }
                var peer = (client.Client.RemoteEndPoint as IPEndPoint)?.Address;
                if (peer is null || !peer.MapToIPv6().Equals(IPAddress.Parse(options.AllowedPeer).MapToIPv6())) { client.Dispose(); Log(new { type = "peer_rejected" }); continue; }
                // All backend/native calls stay on this one dedicated worker, including disposal.
                Task.Factory.StartNew(() => Handle(client, certificate, options, cancellation.Token, Log, Status),
                    CancellationToken.None, TaskCreationOptions.LongRunning, TaskScheduler.Default).GetAwaiter().GetResult();
                Status(true, null, false);
            }
        }
        finally { listener.Stop(); Console.CancelKeyPress -= handler; Status(false, null, false); }
        return 0;
    }
    private static void Handle(TcpClient client, X509Certificate2 certificate, V2Options options, CancellationToken cancellation,
        Action<object> log, Action<bool, string?, bool> status)
    {
        V2Protocol? protocol = null;
        using var ownedClient = client;
        using var stream = new SslStream(client.GetStream(), false);
        using var backend = new V2NativeBackend(options, log);
        try
        {
            client.NoDelay = true;
            using (var handshake = CancellationTokenSource.CreateLinkedTokenSource(cancellation))
            {
                handshake.CancelAfter(5000);
                stream.AuthenticateAsServerAsync(new SslServerAuthenticationOptions { ServerCertificate = certificate,
                    ClientCertificateRequired = false, EnabledSslProtocols = SslProtocols.Tls12 | SslProtocols.Tls13 }, handshake.Token).GetAwaiter().GetResult();
                using var hello = Framing.Read(stream, handshake.Token).GetAwaiter().GetResult();
                var root = hello.RootElement;
                if (!V2Protocol.Authenticate(root, options.Token)) throw new InvalidDataException("authentication_failed");
            }
            var time = Stopwatch.StartNew(); protocol = new V2Protocol(backend, () => time.ElapsedMilliseconds);
            void Send(object value)
            {
                using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellation); timeout.CancelAfter(3000);
                Framing.Write(stream, value, timeout.Token).GetAwaiter().GetResult();
            }
            Send(protocol.Hello()); status(true, protocol.Epoch, true); log(new { type = "authenticated", epoch = protocol.Epoch, NativeActions = 0 });
            using var readLifetime = CancellationTokenSource.CreateLinkedTokenSource(cancellation);
            readLifetime.CancelAfter(7000);
            Task<JsonDocument>? pending = null;
            try
            {
                pending = Framing.Read(stream, readLifetime.Token);
                while (!cancellation.IsCancellationRequested)
                {
                    foreach (var reply in protocol.Tick()) { Send(reply); log(reply); }
                    if (!pending.IsCompleted) { Thread.Sleep(25); continue; }
                    using var message = pending.GetAwaiter().GetResult(); pending = null;
                    var type = Framing.Type(message.RootElement);
                    foreach (var reply in protocol.Process(message.RootElement)) { Send(reply); if (type != "ping") log(reply); }
                    readLifetime.CancelAfter(7000);
                    pending = Framing.Read(stream, readLifetime.Token);
                }
            }
            finally
            {
                readLifetime.Cancel();
                if (pending is not null) _ = pending.ContinueWith(task =>
                {
                    if (task.IsCompletedSuccessfully) task.Result.Dispose();
                    else _ = task.Exception;
                }, CancellationToken.None, TaskContinuationOptions.ExecuteSynchronously, TaskScheduler.Default);
            }
        }
        catch (Exception error) { log(new { type = "client_closed", error = error.GetType().Name,
            hresult = error.HResult, inner_error = error.InnerException?.GetType().Name, inner_hresult = error.InnerException?.HResult, epoch = protocol?.Epoch }); }
        finally { protocol?.Close(); }
    }
}
