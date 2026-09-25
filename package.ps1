# Assemble the FOMOD-installable archive.
#
#   .\package.ps1                       # -> package/MMO Hotbar.7z
#   .\package.ps1 -NoArchive            # leave the staged tree, don't compress
#
# The two keycap SWFs ship from flash/<set>/STB_Keycaps.swf, each with its credit note at
# flash/<set>/credits.txt. That art is NOT ours and NOT covered by our GPL-3.0: it belongs
# to the SkyUI Team, and to Vor/Vorganger + uranreactor for the Untarnished set, and it may
# only be redistributed with them credited. The note is copied into the installer beside the
# art -- leave both files in place. A set with no SWF is skipped, and this script says which;
# see flash/README.md.
#
# 7z.exe is only needed for the archive step -- without it the staged tree is left as is.

[CmdletBinding()]
param(
    [string]$Configuration = 'Release',
    [switch]$NoArchive
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$stage = Join-Path $root 'package/stage'
$dll = Join-Path $root "build/$Configuration/MMOHotbar.dll"

if (-not (Test-Path -LiteralPath $dll)) {
    throw "No plugin at $dll -- build it first (cmake --build build --config $Configuration)."
}

if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage -Force | Out-Null

# --- always installed -----------------------------------------------------------------
$core = Join-Path $stage 'core'
New-Item -ItemType Directory -Path (Join-Path $core 'SKSE/Plugins') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $core 'Interface') -Force | Out-Null
Copy-Item -LiteralPath $dll -Destination (Join-Path $core 'SKSE/Plugins')
Copy-Item -Path (Join-Path $root 'dist/SKSE/Plugins/*.ini') -Destination (Join-Path $core 'SKSE/Plugins')
Copy-Item -Path (Join-Path $root 'dist/Interface/*.txt') -Destination (Join-Path $core 'Interface')

# The hotbar HUD movie. It ships from dist/Interface/MMOHotbar/ and contains no third-party
# art: item icons and keycaps are loaded at run time from the SWF files the player already
# has installed (SkyUI's icons, and whichever STB_Keycaps.swf the installer picked above).
#
# This one is a hard error rather than a warning. Hotbar.swf is OUR build and is tracked in
# git (.gitignore re-includes it), so if it is absent the checkout is broken -- and the
# alternative, shipping an installer whose headline feature silently does nothing, is worse
# than a failed build. It also cannot be rebuilt on CI: the source slot art is local-only.
$hud = Join-Path $root 'dist/Interface/MMOHotbar'
$hudSwf = Join-Path $hud 'Hotbar.swf'
if (-not (Test-Path -LiteralPath $hudSwf)) {
    throw "dist/Interface/MMOHotbar/Hotbar.swf is missing. It is tracked in git; run 'git status' -- if it is deleted, restore it with 'git checkout -- dist/Interface/MMOHotbar/Hotbar.swf'. Rebuilding it needs the local slot art: python tools/build_hud_swf.py"
}
Copy-Item -LiteralPath $hud -Destination (Join-Path $core 'Interface') -Recurse

# api/MMOHotbarPapyrus.psc is deliberately NOT installed. The plugin registers the
# MMOHotbar natives itself, so the mod is complete without it, and a .psc dropped into
# Scripts/ is inert anyway -- only a compiled .pex runs. It is tracked in the repository as
# the reference for script authors; anyone who wants the wrapper compiles it themselves.

# --- one keycap set, chosen in the installer -------------------------------------------
foreach ($set in 'SkyUI', 'Untarnished') {
    $swf = Join-Path $root "flash/$set/STB_Keycaps.swf"
    if (-not (Test-Path -LiteralPath $swf)) {
        Write-Warning "flash/$set/STB_Keycaps.swf is missing -- the '$set' option will install nothing. See flash/$set/README.md."
        continue
    }
    $dest = Join-Path $stage "keycaps/$set/Interface"
    New-Item -ItemType Directory -Path $dest -Force | Out-Null
    Copy-Item -LiteralPath $swf -Destination $dest
    # The art is not ours: its credit note travels with it. Its own file, not buried in
    # Interface/ -- that is the layout upstream's keycaps download uses as well.
    $credits = Join-Path $root "flash/$set/credits.txt"
    if (Test-Path -LiteralPath $credits) {
        Copy-Item -LiteralPath $credits -Destination (Join-Path $stage "keycaps/$set/Keycap art credits.txt")
    } else {
        Write-Warning "flash/$set/credits.txt is missing -- the art would ship uncredited. See flash/README.md."
    }
}

Copy-Item -Path (Join-Path $root 'fomod') -Destination $stage -Recurse
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination $stage
Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination $stage

Write-Host "Staged:" -ForegroundColor Cyan
Get-ChildItem -LiteralPath $stage -Recurse -File | ForEach-Object {
    '  {0}' -f $_.FullName.Substring($stage.Length + 1)
}

if ($NoArchive) { return }

$sevenZip = (Get-Command '7z.exe' -ErrorAction SilentlyContinue).Source
if (-not $sevenZip) {
    # Installed but not on PATH is the common case on Windows.
    $sevenZip = @("$env:ProgramFiles\7-Zip\7z.exe",
        "${env:ProgramFiles(x86)}\7-Zip\7z.exe") |
        Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if (-not $sevenZip) {
    Write-Warning "7z.exe not found -- staged tree left at $stage, compress it yourself."
    return
}
$archive = Join-Path $root 'package/MMO Hotbar.7z'
if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
& $sevenZip a -t7z -mx=9 $archive (Join-Path $stage '*') | Out-Null
Write-Host "Wrote $archive" -ForegroundColor Green
