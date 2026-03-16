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

function Get-ImportedDllNames {
    param([string]$FilePath)

    if (-not (Get-Command objdump -ErrorAction SilentlyContinue) -or -not (Test-Path -Path $FilePath -PathType Leaf)) {
        return @()
    }

    return @(objdump -p $FilePath | Select-String "DLL Name" | ForEach-Object {
        if ($_ -match "DLL Name:\s*([^\s]+)") {
            $matches[1].ToLowerInvariant()
        }
    } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique)
}

function Is-SystemDll {
    param([string]$DllName)

    $systemDlls = @(
        "kernel32.dll", "user32.dll", "gdi32.dll", "winmm.dll", "ws2_32.dll", "shell32.dll", "msvcrt.dll",
        "advapi32.dll", "ole32.dll", "oleaut32.dll", "comdlg32.dll", "comctl32.dll", "rpcrt4.dll", "imm32.dll",
        "version.dll", "setupapi.dll", "crypt32.dll", "winspool.drv", "uxtheme.dll", "dwmapi.dll", "shlwapi.dll",
        "secur32.dll", "ntdll.dll"
    )

    return $systemDlls -contains ($DllName.ToLowerInvariant())
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

if (Get-Command objdump -ErrorAction SilentlyContinue) {
    $hintedDirs = @("C:\msys64\mingw64\bin", "D:\a\_temp\msys64\mingw64\bin")
    if (-not [string]::IsNullOrWhiteSpace($RaylibDllPath) -and (Test-Path -Path $RaylibDllPath -PathType Leaf)) {
        $hintedDirs += (Split-Path -Parent $RaylibDllPath)
    }
    $hintedDirs = @($hintedDirs | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique)

    $queue = New-Object System.Collections.Generic.Queue[string]
    $visited = New-Object System.Collections.Generic.HashSet[string]
    $copied = New-Object System.Collections.Generic.HashSet[string]

    $queue.Enqueue((Resolve-Path $BinaryPath).Path)

    while ($queue.Count -gt 0) {
        $current = $queue.Dequeue()
        if (-not $visited.Add($current.ToLowerInvariant())) {
            continue
        }

        $imports = Get-ImportedDllNames -FilePath $current
        foreach ($dllName in $imports) {
            if (Is-SystemDll $dllName) {
                continue
            }

            $hints = @()
            if (-not [string]::IsNullOrWhiteSpace($RaylibDllPath) -and
                ([System.IO.Path]::GetFileName($RaylibDllPath).ToLowerInvariant() -eq $dllName) -and
                (Test-Path -Path $RaylibDllPath -PathType Leaf)) {
                $hints += $RaylibDllPath
            }
            foreach ($dir in $hintedDirs) {
                $hints += (Join-Path $dir $dllName)
            }

            $resolvedDll = Resolve-DllPath -DllName $dllName -Hints $hints
            if (-not $resolvedDll) {
                throw "Binary imports $dllName but it was not found. Pass -RaylibDllPath or ensure required DLLs are in PATH."
            }

            $resolvedKey = $resolvedDll.ToLowerInvariant()
            if ($copied.Add($resolvedKey)) {
                Copy-Item $resolvedDll $stageDir
                $queue.Enqueue($resolvedDll)
            }
        }
    }
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
