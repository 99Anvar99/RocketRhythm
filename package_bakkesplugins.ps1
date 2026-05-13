param(
    [string]$ProjectDir = ".",
    [string]$OutputDir = "dist",
    [string]$PluginDll = ""
)

$ErrorActionPreference = "Stop"

$root = (Resolve-Path $ProjectDir).Path
$dist = Join-Path $root $OutputDir
$stage = Join-Path $dist "RocketRhythm-release"

$versionHeader = Join-Path $root "version.h"
$versionText = "2.0.0"
if (Test-Path $versionHeader) {
    $content = Get-Content -LiteralPath $versionHeader -Raw
    $major = [regex]::Match($content, "#define\s+VERSION_MAJOR\s+(\d+)")
    $minor = [regex]::Match($content, "#define\s+VERSION_MINOR\s+(\d+)")
    $patch = [regex]::Match($content, "#define\s+VERSION_PATCH\s+(\d+)")
    if ($major.Success -and $minor.Success -and $patch.Success) {
        $versionText = "$($major.Groups[1].Value).$($minor.Groups[1].Value).$($patch.Groups[1].Value)"
    }
}

$zipPath = Join-Path $dist "RocketRhythm-$versionText-release.zip"

New-Item -ItemType Directory -Force -Path $dist | Out-Null
if (Test-Path $stage) {
    Remove-Item -LiteralPath $stage -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $stage | Out-Null

$resolvedPluginDll = $PluginDll
if ([string]::IsNullOrWhiteSpace($resolvedPluginDll)) {
    $resolvedPluginDll = Join-Path $root "plugins\RocketRhythm.dll"
}

if (-not (Test-Path $resolvedPluginDll)) {
    throw "Plugin DLL was not found: $resolvedPluginDll"
}

$pluginTargetDir = Join-Path $stage "plugins"
New-Item -ItemType Directory -Force -Path $pluginTargetDir | Out-Null
Copy-Item -LiteralPath $resolvedPluginDll -Destination (Join-Path $pluginTargetDir "RocketRhythm.dll") -Force

if (Test-Path $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}

Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zipPath -Force
Remove-Item -LiteralPath $stage -Recurse -Force

Write-Host "Created BakkesPlugins release zip: $zipPath"
