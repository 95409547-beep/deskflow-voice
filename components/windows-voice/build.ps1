# SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
param([switch]$Publish, [string]$Dotnet='dotnet')
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
Push-Location $root
$artifacts=Join-Path $root 'artifacts'
$project=Join-Path $root 'Deskflow.WindowsVoice.csproj'
$output=Join-Path $artifacts 'check'
[void][IO.Directory]::CreateDirectory($artifacts)
$names=@('TEMP','TMP','NUGET_PACKAGES','DOTNET_CLI_HOME','DOTNET_CLI_TELEMETRY_OPTOUT','DOTNET_CLI_UI_LANGUAGE','DOTNET_GENERATE_ASPNET_CERTIFICATE','DOTNET_SKIP_FIRST_TIME_EXPERIENCE')
$previous=@{};foreach($name in $names){$previous[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
$temp=Join-Path $artifacts 'temp';[void][IO.Directory]::CreateDirectory($temp)
[Environment]::SetEnvironmentVariable('TEMP',$temp,'Process');[Environment]::SetEnvironmentVariable('TMP',$temp,'Process')
[Environment]::SetEnvironmentVariable('NUGET_PACKAGES',(Join-Path $artifacts 'packages'),'Process')
[Environment]::SetEnvironmentVariable('DOTNET_CLI_HOME',(Join-Path $artifacts 'dotnet-home'),'Process')
[Environment]::SetEnvironmentVariable('DOTNET_CLI_TELEMETRY_OPTOUT','1','Process')
[Environment]::SetEnvironmentVariable('DOTNET_CLI_UI_LANGUAGE','en','Process')
[Environment]::SetEnvironmentVariable('DOTNET_GENERATE_ASPNET_CERTIFICATE','false','Process')
[Environment]::SetEnvironmentVariable('DOTNET_SKIP_FIRST_TIME_EXPERIENCE','1','Process')
try {
    & $Dotnet build $project -c Release --output $output --artifacts-path (Join-Path $artifacts 'build') --disable-build-servers -p:UseSharedCompilation=false -nodeReuse:false --nologo
    if($LASTEXITCODE){throw 'Build failed'}
    $dll=Join-Path $output 'deskflow-windows-voice.dll'
    & $Dotnet $dll
    if($LASTEXITCODE){throw 'Default help failed'}
    & $Dotnet $dll --help
    if($LASTEXITCODE){throw 'Explicit help failed'}
    $raw=& $Dotnet $dll --check-managed
    if($LASTEXITCODE){throw 'Managed checks failed'}
    $raw|Set-Content -LiteralPath (Join-Path $artifacts 'managed-checks.json') -Encoding UTF8
    $raw
    if($Publish){
        $published=Join-Path $artifacts 'publish\win-x64'
        & $Dotnet publish $project -c Release -p:PublishProfile=win-x64 --output $published --artifacts-path (Join-Path $artifacts 'publish-build') --disable-build-servers -p:UseSharedCompilation=false -nodeReuse:false --nologo
        if($LASTEXITCODE){throw 'Self-contained publish failed'}
        & (Join-Path $published 'deskflow-windows-voice.exe') --check-managed
        if($LASTEXITCODE){throw 'Published managed checks failed'}
    }
} finally {
    foreach($name in $names){[Environment]::SetEnvironmentVariable($name,$previous[$name],'Process')}
    Pop-Location
}
