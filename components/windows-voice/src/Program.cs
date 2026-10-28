// SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
using System.Text.Json;

namespace DeskflowVoice.Windows;

internal enum Command { Help, ManagedChecks, ServeV2, Invalid }
internal static class CommandLine
{
    internal static Command Parse(string[] args) => args.Length == 2 && args[0] == "--serve-v2" && !string.IsNullOrWhiteSpace(args[1])
        ? Command.ServeV2 : args.Length == 0 ? Command.Help : args.Length != 1 ? Command.Invalid : args[0] switch
    {
        "--help" or "-h" => Command.Help,
        "--check-managed" => Command.ManagedChecks,
        _ => Command.Invalid
    };
}

internal static class Program
{
    private static int Main(string[] args)
    {
        var command = CommandLine.Parse(args);
        if (command is Command.Help or Command.Invalid)
        {
            Console.WriteLine("Deskflow Windows voice component\nUsage: deskflow-windows-voice [--help | --check-managed | --serve-v2 <private-config>]\nDefault: help. V2 native input requires an authenticated BEGIN; no hooks or synthetic keys are installed.");
            return command == Command.Help ? 0 : 2;
        }
        if (!OperatingSystem.IsWindows()) { Console.Error.WriteLine("The Windows component checks require Windows."); return 2; }
        try
        {
            if (command == Command.ServeV2) return V2Server.Run(V2Options.Load(args[1]));
            Console.WriteLine(JsonSerializer.Serialize(ManagedChecks.Run(), new JsonSerializerOptions { WriteIndented = true }));
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine((command == Command.ServeV2 ? "V2 receiver failed: " : "Managed checks failed: ") + error.Message);
            return 1;
        }
    }
}
