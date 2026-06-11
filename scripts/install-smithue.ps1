<#
.SYNOPSIS
    Idempotent installer for the SmithUE plugin into any Unreal Engine project.

.DESCRIPTION
    Copies or symlinks the SmithUE plugin directory into a target project's Plugins folder.
    Supports idempotent re-runs, backup on -Force, and both copy and symlink modes.

.PARAMETER ProjectPluginsDir
    Path to the target project's Plugins directory (e.g. C:\MyGame\Plugins).

.PARAMETER Source
    Path to the SmithUE plugin root (must contain SmithUE.uplugin).
    Defaults to the parent of the scripts/ directory (i.e. the repo root).

.PARAMETER Mode
    Installation mode: "copy" (recursive copy) or "symlink" (directory junction/symlink).
    Default: symlink.

.PARAMETER EngineLevel
    When set, treats $ProjectPluginsDir as-is (caller passes engine plugins dir directly).
    The SmithUE subfolder is NOT appended.

.PARAMETER Force
    If target already exists, rename it to SmithUE.bak-<timestamp> and proceed.

.EXAMPLE
    # Copy install into a project
    .\install-smithue.ps1 -ProjectPluginsDir "C:\MyGame\Plugins" -Mode copy

.EXAMPLE
    # Symlink install (default)
    .\install-smithue.ps1 -ProjectPluginsDir "C:\MyGame\Plugins"

.EXAMPLE
    # Force-replace existing install
    .\install-smithue.ps1 -ProjectPluginsDir "C:\MyGame\Plugins" -Mode copy -Force
#>

param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectPluginsDir,

    [string]$Source = (Join-Path $PSScriptRoot ".."),

    [ValidateSet("copy", "symlink")]
    [string]$Mode = "symlink",

    [switch]$EngineLevel,

    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ─── 1. Resolve & validate source ────────────────────────────────────────────
$Source = (Resolve-Path $Source).Path
$upluginInSource = Join-Path $Source "SmithUE.uplugin"

if (-not (Test-Path $upluginInSource)) {
    Write-Error "Source '$Source' does not look like a SmithUE plugin root — SmithUE.uplugin not found."
    exit 3
}

# ─── 2. Resolve target ───────────────────────────────────────────────────────
if ($EngineLevel) {
    $target = $ProjectPluginsDir
} else {
    $target = Join-Path $ProjectPluginsDir "SmithUE"
}

# ─── 3. Handle existing target ───────────────────────────────────────────────
if (Test-Path $target) {
    $item = Get-Item -LiteralPath $target -Force

    # Idempotency check: already a symlink/junction pointing at correct source?
    $isLink = ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0
    if ($isLink) {
        $linkTarget = $item.Target  # PowerShell 5+: .Target on FileInfo/DirectoryInfo
        # Normalise both paths for comparison (resolve any trailing slashes, case)
        $normSource = $Source.TrimEnd('\', '/')
        $normLink   = if ($linkTarget) { $linkTarget.TrimEnd('\', '/') } else { "" }

        if ($normLink -eq $normSource) {
            Write-Host "Already installed (symlink pointing to '$Source'). Nothing to do." -ForegroundColor Green
            exit 0
        }
    }

    if (-not $Force) {
        Write-Host ""
        Write-Host "SmithUE already exists at '$target'." -ForegroundColor Yellow
        Write-Host "Use -Force to replace it (the existing copy will be backed up as SmithUE.bak-<timestamp>)." -ForegroundColor Yellow
        Write-Host ""
        Write-Host "Example:"
        Write-Host "  .\install-smithue.ps1 -ProjectPluginsDir '$ProjectPluginsDir' -Mode $Mode -Force"
        Write-Host ""
        exit 1
    }

    # -Force: back up existing
    $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $backup = Join-Path (Split-Path $target -Parent) "SmithUE.bak-$timestamp"
    Write-Host "Backing up existing '$target' → '$backup'" -ForegroundColor Cyan
    Rename-Item -LiteralPath $target -NewName (Split-Path $backup -Leaf)
}

# ─── 4. Create parent directory if needed ────────────────────────────────────
$parentDir = Split-Path $target -Parent
if (-not (Test-Path $parentDir)) {
    Write-Host "Creating directory: $parentDir" -ForegroundColor Cyan
    New-Item -ItemType Directory -Path $parentDir -Force | Out-Null
}

# ─── 5. Install ──────────────────────────────────────────────────────────────
if ($Mode -eq "symlink") {
    Write-Host "Creating symlink: '$target' → '$Source'" -ForegroundColor Cyan
    try {
        New-Item -ItemType SymbolicLink -Path $target -Target $Source | Out-Null
    } catch {
        Write-Host ""
        Write-Host "Failed to create symbolic link: $_" -ForegroundColor Red
        Write-Host ""
        Write-Host "Symlinks on Windows typically require either:"
        Write-Host "  • Administrator privileges, or"
        Write-Host "  • Developer Mode enabled (Settings → Developer Mode)"
        Write-Host ""
        Write-Host "Try again with -Mode copy to perform a plain recursive copy instead:"
        Write-Host "  .\install-smithue.ps1 -ProjectPluginsDir '$ProjectPluginsDir' -Mode copy"
        Write-Host ""
        exit 2
    }
} else {
    # copy mode
    Write-Host "Copying '$Source' → '$target'" -ForegroundColor Cyan
    Copy-Item -Recurse -Path $Source -Destination $target
}

# ─── 6. Verify ───────────────────────────────────────────────────────────────
$upluginAtDest = Join-Path $target "SmithUE.uplugin"
if (-not (Test-Path $upluginAtDest)) {
    Write-Host ""
    Write-Host "Installation appeared to succeed but SmithUE.uplugin was NOT found at '$upluginAtDest'." -ForegroundColor Red
    Write-Host "Please check the source and target paths and try again." -ForegroundColor Red
    exit 4
}

# ─── 7. Success ──────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "SmithUE installed successfully!" -ForegroundColor Green
Write-Host "  Mode   : $Mode"
Write-Host "  Source : $Source"
Write-Host "  Target : $target"
Write-Host ""
Write-Host "Next steps:"
Write-Host "  1. Open your Unreal Engine project — UE will detect and compile SmithUE automatically."
Write-Host "  2. Once the editor loads, verify with: npx smithue-cli status"
Write-Host ""
exit 0
