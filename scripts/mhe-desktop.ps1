# mod-hw-emulator - runs MOD Desktop with an emulated controller, the Dwarf or the Duo X (Windows)
#
#   mhe-desktop.cmd [-Device dwarf|duox] [-ModDesktop DIR] [-Overlay DIR] [-NoEmulator] [-Refresh] [-SharedData]
#
# In the standalone package (package-win64.sh --standalone) MOD Desktop is in mod-desktop\ next to this script,
# with these files already in it: -ModDesktop, -Overlay and -Refresh do not apply, and its data is in data# (Documents\MOD Desktop with -SharedData, the folder of an installed MOD Desktop).
#
# MOD Desktop stays as it is installed. This keeps a copy of it in -Overlay (by default
# %LOCALAPPDATA%\mod-hw-emulator\mod-desktop, refreshed when the installed files change) with these files changed:
#
#   jack\mod-host.dll            mod-host with the HMI control (MOD_HMI_EMULATOR), messages over TCP
#   mod\hmi.py                   mod-ui's HMI line over TCP as well as over a serial port
#   html\js\hardware.js,         hardware addressing (the Device tab) open, as the descriptor has actuators
#   html\js\desktop-app.js
#   mod-desktop.exe              the launcher with an "Open Emulator" button (MOD_HW_EMULATOR below)
#   mod-hardware-descriptor.json MOD Desktop's descriptor with the actuators and HMI options of -Device
#
# (all but the descriptor are in mod-desktop-overlay, at their place in the installation)
#
# then starts the emulator (unless -NoEmulator: one already running) and the copy of MOD Desktop, which talk to
# each other over 127.0.0.1: 9900 for mod-ui's HMI line, 9901 for mod-host's /sys_msgs. The user data (pedalboards,
# plugins, settings in Documents\MOD Desktop) is the same as with the installed MOD Desktop: quit that one first.

param(
    [ValidateSet('dwarf', 'duox')]
    [string]$Device = 'dwarf',
    [string]$ModDesktop = "$env:ProgramFiles\MOD Desktop",
    [string]$Overlay = "$env:LOCALAPPDATA\mod-hw-emulator\mod-desktop",
    [switch]$NoEmulator,
    [switch]$Refresh,
    [switch]$SharedData
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$files = Join-Path $here 'mod-desktop-overlay'
$hmiPort = 9900
# the emulator's EEPROM and settings, one per device, the same whoever starts it
$state = Join-Path $here "mhe-state\$Device"
$firmware = Join-Path $here "fw-$Device.dll"
$sysPort = 9901

# the standalone package has its own MOD Desktop, the overlay already laid over it
$portable = Join-Path $here 'mod-desktop'
if (Test-Path (Join-Path $portable 'mod-desktop.exe')) {
    $Overlay = $portable
    $desktopDescriptor = Join-Path $here 'devices\mod-desktop.json'
    # its data (pedalboards, user files, the launcher log) next to it, unless -SharedData
    if (-not $SharedData) {
        $env:MOD_DATA_DIR = Join-Path $here 'data'
    }
} else {
    if (-not (Test-Path (Join-Path $ModDesktop 'mod-desktop.exe'))) {
        throw "no MOD Desktop in '$ModDesktop': install it, or give its folder with -ModDesktop"
    }
    $desktopDescriptor = Join-Path $ModDesktop 'mod-hardware-descriptor.json'

    # the overlay is made for one mod-ui: check that the installed files are the ones it changes
    foreach ($line in Get-Content (Join-Path $files 'base-sha256.txt')) {
        $hash, $path = $line.Trim() -split ' ', 2
        $installed = Join-Path $ModDesktop $path
        if (-not (Test-Path $installed) -or (Get-FileHash $installed -Algorithm SHA256).Hash -ne $hash) {
            Write-Warning "this MOD Desktop's $path is not the one the overlay was made for: mod-ui may not work"
        }
    }

    # copy of the installation: robocopy copies only what changed since the last time
    if ($Refresh -and (Test-Path $Overlay)) {
        Remove-Item -Recurse -Force $Overlay
    }
    New-Item -ItemType Directory -Force $Overlay | Out-Null
    robocopy $ModDesktop $Overlay /MIR /NFL /NDL /NJH /NJS /NP /R:1 /W:1 | Out-Null
    if ($LASTEXITCODE -ge 8) {
        throw "copying '$ModDesktop' to '$Overlay' failed (robocopy $LASTEXITCODE)"
    }

    robocopy $files $Overlay /E /XF base-sha256.txt /NFL /NDL /NJH /NJS /NP /R:1 /W:1 | Out-Null
    if ($LASTEXITCODE -ge 8) {
        throw "laying '$files' over '$Overlay' failed (robocopy $LASTEXITCODE)"
    }
}

# MOD Desktop's descriptor keeps what tells the desktop apart (name, binaries); the device's one gives the rest
$desktop = Get-Content -Raw $desktopDescriptor | ConvertFrom-Json
$descriptor = Get-Content -Raw (Join-Path $here "devices\$Device\mod-hardware-descriptor.json") | ConvertFrom-Json
foreach ($key in 'name', 'architecture', 'bin-compat') {
    if ($desktop.PSObject.Properties[$key]) {
        $descriptor | Add-Member -Force -NotePropertyName $key -NotePropertyValue $desktop.$key
    }
}
$json = $descriptor | ConvertTo-Json -Depth 20
[IO.File]::WriteAllText((Join-Path $Overlay 'mod-hardware-descriptor.json'), $json, (New-Object Text.UTF8Encoding $false))

function Test-Listening([int]$port) {
    $client = New-Object Net.Sockets.TcpClient
    try {
        $client.Connect('127.0.0.1', $port)
        return $true
    } catch {
        return $false
    } finally {
        $client.Close()
    }
}

# the emulator first: mod-host connects to it when it starts
if (-not $NoEmulator) {
    if (Test-Listening $sysPort) {
        Write-Host "something is already on port ${sysPort}: an emulator running? (-NoEmulator to use it)"
        exit 1
    }
    Start-Process -FilePath (Join-Path $here 'mod-hw-emulator.exe') `
                  -ArgumentList '--state', ('"{0}"' -f $state), '--tcp', "webgui=$hmiPort", ('"{0}"' -f $firmware) `
                  -WorkingDirectory $here
}
$deadline = (Get-Date).AddSeconds(15)
while (-not (Test-Listening $sysPort)) {
    if ((Get-Date) -gt $deadline) {
        throw "the emulator is not listening on port $sysPort"
    }
    Start-Sleep -Milliseconds 250
}

# MOD Desktop passes its environment to jackd (mod-host) and mod-ui
$env:MOD_HMI_SERIAL_PORT = "tcp://127.0.0.1:$hmiPort"
$env:MOD_SYS_SERIAL_PORT = "tcp://127.0.0.1:$sysPort"
$env:MOD_DEV_HMI = '0'
# the launcher's "Open Emulator" button runs this, and starts MOD Desktop again if it is running
$env:MOD_HW_EMULATOR = '"{0}" --state "{1}" --tcp webgui={2} "{3}"' -f (Join-Path $here 'mod-hw-emulator.exe'), $state, $hmiPort, $firmware
Start-Process -FilePath (Join-Path $Overlay 'mod-desktop.exe') -WorkingDirectory $Overlay
if ($Overlay -eq $portable) {
    Write-Host "MOD Desktop ($Overlay) started with the emulated $Device"
} else {
    Write-Host "MOD Desktop (copy in $Overlay) started with the emulated $Device"
}
