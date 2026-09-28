# Copyright (C) 2026 Mannai
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds Shutterlink with the MSVC command-line tools.
#   .\build.ps1                    ShutterlinkSource.dll + Shutterlink.exe into .\bin
#   .\build.ps1 -Target tools      diagnostic tools into .\bin\tools
#   .\build.ps1 -Package 0.1.0     also writes .\dist\Shutterlink-0.1.0-win64.zip
param(
    [ValidateSet('source', 'app', 'tools', 'all')][string]$Target = 'all',
    [string]$Package
)

$ErrorActionPreference = 'Stop'
$installer = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer"
$vs = & "$installer\vswhere.exe" -products * -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'MSVC build tools not found (install Visual Studio 2022 Build Tools with the C++ workload)' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$env:PATH = "$installer;$env:PATH"  # vcvars calls vswhere by name

$common = '/nologo /utf-8 /std:c++20 /EHsc /O2 /W4 /MT /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX'

function Invoke-Vc([string]$command) {
    cmd /c "`"$vcvars`" >nul && $command"
    if ($LASTEXITCODE -ne 0) { throw "failed: $command" }
}

function Invoke-Cl([string]$outDir, [string]$clArgs) {
    New-Item -ItemType Directory -Force $outDir | Out-Null
    Invoke-Vc "cl $common /Fo$outDir\ $clArgs"
}

Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Force bin | Out-Null
    if ($Target -in 'source', 'all') {
        $src = (Get-ChildItem src\source\*.cpp).FullName -join ' '
        Invoke-Cl 'obj\source' "/LD $src /Febin\ShutterlinkSource.dll /link /DEF:src\source\ShutterlinkSource.def mfplat.lib mfuuid.lib mf.lib ole32.lib advapi32.lib runtimeobject.lib"
    }
    if ($Target -in 'app', 'all') {
        New-Item -ItemType Directory -Force obj\app | Out-Null
        Invoke-Vc 'rc /nologo /fo obj\app\shutterlink.res src\app\shutterlink.rc'
        $src = (Get-ChildItem src\app\*.cpp).FullName -join ' '
        Invoke-Cl 'obj\app' "$src obj\app\shutterlink.res /Febin\Shutterlink.exe /link /SUBSYSTEM:WINDOWS mfplat.lib mfuuid.lib mfsensorgroup.lib windowscodecs.lib PortableDeviceGUIDs.lib ole32.lib oleaut32.lib shell32.lib user32.lib gdi32.lib advapi32.lib"
    }
    if ($Target -eq 'tools') {
        New-Item -ItemType Directory -Force bin\tools | Out-Null
        Invoke-Cl 'obj\tools' "tools\cam_test.cpp /Febin\tools\cam_test.exe mfplat.lib mf.lib mfreadwrite.lib mfuuid.lib ole32.lib"
        foreach ($probe in Get-ChildItem tools\probe\*.cpp) {
            $exe = "bin\tools\$($probe.BaseName).exe"
            Invoke-Cl "obj\tools\$($probe.BaseName)" "/Isrc\app $($probe.FullName) src\app\wpd_ptp.cpp /Fe$exe PortableDeviceGUIDs.lib ole32.lib oleaut32.lib"
        }
    }
    if ($Package) {
        $stage = "obj\package\Shutterlink-$Package"
        Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
        New-Item -ItemType Directory -Force $stage, dist | Out-Null
        Copy-Item bin\Shutterlink.exe, bin\ShutterlinkSource.dll, README.md, LICENSE $stage
        $zip = "dist\Shutterlink-$Package-win64.zip"
        Remove-Item $zip -ErrorAction SilentlyContinue
        Compress-Archive -Path "$stage\*" -DestinationPath $zip
        "packaged $zip"
    }
} finally {
    Pop-Location
}
