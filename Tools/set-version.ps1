<#
  BUSMASTER-NG: sets the version number everywhere it is written in the sources, and draws the splash screen.
  Replaces update-version-information.sh of the original project (its paths no longer exist).

  Usage (Windows PowerShell), before building a release:
    .\Tools\set-version.ps1 -Version 3.4.0 [-CodeName Overdrive]

  Changed: Application\BUSMASTER.rc and BUSMASTERJPN.rc (version resource and IDS_VERSION, shown in About and in
  the logs), Application\StdAfx.h (VERSION_MAJOR/MINOR/BUILD), BUSMASTER_Settings\BUSMASTER.props (BUSMASTER_VER),
  the example in the bug report form, and the splash screen (make-splash.ps1).
  Not changed on purpose: the version markers inside file formats ([3.2.2] in .dbf and in the converters), so that
  files stay readable by BUSMASTER 3.2.x.
#>
param(
    [Parameter(Mandatory = $true)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
    [string]$CodeName = "Overdrive"
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$app = Join-Path $root "Sources\BUSMASTER\Application"
$parts = $Version.Split('.')
$comma = "$($parts[0]),$($parts[1]),$($parts[2]),0"

# Replace in a file keeping its bytes (encoding, line endings); each pattern must be found.
# A list with one rule is written ,@(...) so that PowerShell does not flatten it.
function Update-File([string]$Path, [object[]]$Rules) {
    $enc = [System.Text.Encoding]::GetEncoding(1252)
    $text = $enc.GetString([System.IO.File]::ReadAllBytes($Path))
    foreach ($rule in $Rules) {
        if ($text -notmatch $rule[0]) { throw "Not found in ${Path}: $($rule[0])" }
        $text = [regex]::Replace($text, $rule[0], $rule[1])
    }
    [System.IO.File]::WriteAllBytes($Path, $enc.GetBytes($text))
    Write-Host "  $Path"
}

$rcRules = @(
    @('(?m)^( FILEVERSION )\d+,\d+,\d+,\d+', "`${1}$comma"),
    @('(?m)^( PRODUCTVERSION )\d+,\d+,\d+,\d+', "`${1}$comma"),
    @('(VALUE "FileVersion", "Ver )\d+\.\d+\.\d+', "`${1}$Version"),
    @('(VALUE "ProductVersion", "Ver )\d+\.\d+\.\d+', "`${1}$Version"),
    @('(IDS_VERSION\s+"Ver )\d+\.\d+\.\d+', "`${1}$Version")
)
Write-Host "Setting version $Version"
Update-File (Join-Path $app "BUSMASTER.rc") $rcRules
Update-File (Join-Path $app "BUSMASTERJPN.rc") $rcRules
Update-File (Join-Path $app "StdAfx.h") @(
    @('(?m)^//\d+\.\d+\.\d+', "//$Version"),
    @('const BYTE VERSION_MAJOR=\d+;', "const BYTE VERSION_MAJOR=$($parts[0]);"),
    @('const BYTE VERSION_MINOR=\d+;', "const BYTE VERSION_MINOR=$($parts[1]);"),
    @('const BYTE VERSION_BUILD=\d+;', "const BYTE VERSION_BUILD=$($parts[2]);")
)
Update-File (Join-Path $root "Sources\BUSMASTER\BUSMASTER_Settings\BUSMASTER.props") @(
    ,@('BUSMASTER_VER="\d+\.\d+\.\d+"', "BUSMASTER_VER=""$Version""")
)
Update-File (Join-Path $root ".github\ISSUE_TEMPLATE\bug_report.yml") @(
    ,@('Ver \d+\.\d+\.\d+', "Ver $Version")
)
& (Join-Path $PSScriptRoot "make-splash.ps1") -Version $Version -CodeName $CodeName
Write-Host "Done. Rebuild BUSMASTER (the version is in BUSMASTER.exe) before make-release.ps1."
