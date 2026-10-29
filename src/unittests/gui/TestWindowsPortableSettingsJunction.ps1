# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
[CmdletBinding()]
param([Parameter(Mandatory)][string]$PackagePath)
$ErrorActionPreference='Stop'
if(-not $IsWindows){throw 'windows_only'}
$package=[IO.Path]::GetFullPath($PackagePath)
if(-not (Test-Path -LiteralPath (Join-Path $package 'deskflow.exe')) -or (Test-Path -LiteralPath (Join-Path $package 'settings'))){throw 'use_an_unconfigured_public_package'}
if(@(Get-ChildItem -LiteralPath $package -Recurse -Force | Where-Object {$_.Attributes -band [IO.FileAttributes]::ReparsePoint}).Count){throw 'package_reparse_point_rejected'}
$temporary=[IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$fixture=Join-Path $temporary ('Deskflow Voice junction check-'+[Guid]::NewGuid().ToString('N'))
if(-not ([IO.Path]::GetFullPath($fixture)).StartsWith($temporary,[StringComparison]::OrdinalIgnoreCase)){throw 'fixture_outside_temp'}
$app=Join-Path $fixture 'Application with spaces'
$profile=Join-Path $fixture 'private settings'
$junction=Join-Path $app 'settings'
$process=$null
try {
    [void](New-Item -ItemType Directory -Path $fixture)
    Copy-Item -LiteralPath $package -Destination $app -Recurse
    [void](New-Item -ItemType Directory -Path $profile)
    $receiverFixture='receiver-fixture.json'
    [IO.File]::WriteAllText((Join-Path $profile 'Deskflow Voice.conf'),"[core]`nlanguage=zh_CN`n[gui]`nstartCoreWithGui=false`n[voice]`nenabled=false`nreceiverFile=$receiverFixture`n",[Text.UTF8Encoding]::new($false))
    [void](New-Item -ItemType Junction -Path $junction -Value $profile)
    $start=[Diagnostics.ProcessStartInfo]::new((Join-Path $app 'deskflow.exe'))
    $start.UseShellExecute=$false;$start.CreateNoWindow=$true
    $start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    $start.WorkingDirectory=$app;$start.ArgumentList.Add('--version')
    $process=[Diagnostics.Process]::Start($start)
    $output=$process.StandardOutput.ReadToEndAsync()
    $errors=$process.StandardError.ReadToEndAsync()
    if(-not $process.WaitForExit(15000)){
        # Only this test's freshly created --version process, with voice/core disabled.
        $process.Kill();[void]$process.WaitForExit(5000)
        throw 'portable_settings_junction_startup_or_exit_hung'
    }
    if($process.ExitCode -ne 0 -or $output.GetAwaiter().GetResult() -notmatch 'Deskflow Voice:') {throw 'portable_settings_junction_version_failed'}
    [void]$errors.GetAwaiter().GetResult()
    $saved=[IO.File]::ReadAllText((Join-Path $profile 'Deskflow Voice.conf'))
    $receiverMatch=[regex]::Match($saved,'(?m)^receiverFile=([^\r\n]+)')
    if(-not $receiverMatch.Success){throw 'installed_receiver_path_removed_on_startup'}
    if($receiverMatch.Groups[1].Value -cne $receiverFixture){throw 'installed_receiver_path_changed_on_startup'}
    [pscustomobject]@{JunctionStartupAndExitPassed=$true;ReceiverPathPreserved=$true;ExitCode=0;VoiceEnabled=$false;CoreAutoStart=$false}
} finally {
    if($process){$process.Dispose()}
    if(Test-Path -LiteralPath $junction){[IO.Directory]::Delete($junction,$false)}
    if((Test-Path -LiteralPath $fixture) -and ([IO.Path]::GetFullPath($fixture)).StartsWith($temporary,[StringComparison]::OrdinalIgnoreCase)) {Remove-Item -LiteralPath $fixture -Recurse -Force}
}
