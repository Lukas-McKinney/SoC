param(
    [string]$Version = "dev",
    [string]$OutDir = "dist",
    [string]$BinaryPath = "settlers.exe",
    [string]$RaylibDllPath = ""
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -Path $BinaryPath -PathType Leaf)) {
    throw "Binary '$BinaryPath' not found. Build first with: make"
}

function Resolve-DllPath {
    param(
        [string]$DllName,
        [string[]]$Hints = @()
    )

    foreach ($hint in $Hints) {
        if (-not [string]::IsNullOrWhiteSpace($hint) -and (Test-Path -Path $hint -PathType Leaf)) {
            return (Resolve-Path $hint).Path
        }
    }

    $fromPath = Get-Command $DllName -ErrorAction SilentlyContinue
    if ($fromPath -ne $null -and -not [string]::IsNullOrWhiteSpace($fromPath.Source) -and (Test-Path -Path $fromPath.Source -PathType Leaf)) {
        return (Resolve-Path $fromPath.Source).Path
    }

    return $null
}

$needsRaylibDll = $false
if (Get-Command objdump -ErrorAction SilentlyContinue) {
    $imports = objdump -p $BinaryPath | Select-String "DLL Name" | ForEach-Object { $_.ToString().ToLowerInvariant() }
    if ($imports -match "raylib\.dll") {
        $needsRaylibDll = $true
    }
}

$packageName = "SoC-$Version-windows-x64"
$stageDir = Join-Path $OutDir $packageName
$zipPath = Join-Path $OutDir ("$packageName.zip")
$hashPath = Join-Path $OutDir ("$packageName.sha256.txt")

if (Test-Path $stageDir) {
    Remove-Item -Recurse -Force $stageDir
}
if (Test-Path $zipPath) {
    Remove-Item -Force $zipPath
}
if (Test-Path $hashPath) {
    Remove-Item -Force $hashPath
}

New-Item -ItemType Directory -Path $stageDir -Force | Out-Null

Copy-Item $BinaryPath $stageDir
if (Test-Path "README.md") { Copy-Item "README.md" $stageDir }
if (Test-Path "LICENSE") { Copy-Item "LICENSE" $stageDir }

$resolvedRaylibDll = Resolve-DllPath -DllName "raylib.dll" -Hints @(
    $RaylibDllPath,
    "C:\msys64\mingw64\bin\raylib.dll",
    "D:\a\_temp\msys64\mingw64\bin\raylib.dll"
)

if ($resolvedRaylibDll) {
    Copy-Item $resolvedRaylibDll $stageDir
    $mingwBinDir = Split-Path -Parent $resolvedRaylibDll
    foreach ($runtimeDll in @("libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll")) {
        $runtimePath = Join-Path $mingwBinDir $runtimeDll
        if (Test-Path -Path $runtimePath -PathType Leaf) {
            Copy-Item $runtimePath $stageDir
        }
    }
}
elseif ($needsRaylibDll) {
    throw "Binary imports raylib.dll but it was not found. Pass -RaylibDllPath or ensure raylib.dll is in PATH."
}

@'
@echo off
setlocal
set HOST_PORT=24680
echo Starting host on port %HOST_PORT%...
settlers.exe --host --player red --remote-player blue --port %HOST_PORT%
endlocal
'@ | Set-Content -Path (Join-Path $stageDir "run_host.bat") -Encoding ASCII

@'
@echo off
setlocal
set HOST_IP=127.0.0.1
set HOST_PORT=24680
if not "%~1"=="" set HOST_IP=%~1
echo Joining %HOST_IP%:%HOST_PORT%...
settlers.exe --join %HOST_IP% --player blue --remote-player red --port %HOST_PORT%
endlocal
'@ | Set-Content -Path (Join-Path $stageDir "run_join.bat") -Encoding ASCII

@'
SoC quick start (Windows)

1) Double click run_host.bat on one machine.
2) On the second machine, run_join.bat <HOST_LAN_IP>.
3) If prompted, allow settlers.exe through Windows Firewall.

Notes:
- Use LAN IP, not 127.0.0.1, for remote machines.
- Default port: 24680.
'@ | Set-Content -Path (Join-Path $stageDir "QUICKSTART.txt") -Encoding ASCII

Compress-Archive -Path (Join-Path $stageDir "*") -DestinationPath $zipPath -CompressionLevel Optimal

$hash = (Get-FileHash -Algorithm SHA256 $zipPath).Hash.ToLowerInvariant()
"$hash  $([System.IO.Path]::GetFileName($zipPath))" | Set-Content -Path $hashPath -Encoding ASCII

Write-Host "Created:" $zipPath
Write-Host "SHA256:" $hash
