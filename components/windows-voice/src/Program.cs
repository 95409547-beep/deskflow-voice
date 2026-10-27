// SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
using System.Text.Json;

namespace DeskflowVoice.Windows;

internal enum Command { Help, ManagedChecks, Invalid }
internal static class CommandLine
{
    internal static Command Parse(string[] args) => args.Length == 0 ? Command.Help : args.Length != 1 ? Command.Invalid : args[0] switch
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
            Console.WriteLine("Deskflow Windows voice component (M1)\nUsage: deskflow-windows-voice [--help | --check-managed]\nDefault: help. The CLI exposes no recording, hook, native RPC or deployment command.");
            return command == Command.Help ? 0 : 2;
        }
        if (!OperatingSystem.IsWindows()) { Console.Error.WriteLine("The Windows component checks require Windows."); return 2; }
        try
        {
            Console.WriteLine(JsonSerializer.Serialize(ManagedChecks.Run(), new JsonSerializerOptions { WriteIndented = true }));
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine("Managed checks failed: " + error.Message);
            return 1;
        }
    }
}
