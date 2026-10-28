<#
  BUSMASTER-NG: builds the release package (zip) from a finished build in Sources\BUSMASTER\BIN\Release.

  The package is the Windows 7 compatible build (toolset 14.38, see Sources\Directory.Build.props) with the C++ / MFC
  runtime and the Universal CRT next to the executable, plus the files the original installer used to add:
  node simulation files, MHS driver library, examples and license texts.

  Usage (Windows PowerShell):
    .\tools\make-release.ps1 -Version 3.3.1
    .\tools\make-release.ps1 -Version 3.3.1 -MinGW C:\Tools\mingw32     # also builds the node simulation libraries

  The node simulation libraries (SimulatedSystems\OBJ\libWrapper_*.a) are built with MinGW from
  Sources\BUSMASTER\Application\SimulatedSystems\include. Build them with the same MinGW version the wiki asks users
  to install. Without -MinGW, existing libraries are used; if there are none, the package is made without them and a
  warning is shown.
#>
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$OutDir = "",
    [string]$MinGW = "",
    [string]$VCToolsVersion = "14.38.33130"
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$bin = Join-Path $root "Sources\BUSMASTER\BIN\Release"
$app = Join-Path $root "Sources\BUSMASTER\Application"
if ($OutDir -eq "") { $OutDir = Join-Path $root "release-out" }
$name = "BUSMASTER-NG_$Version"
$stage = Join-Path $OutDir $name
$zip = Join-Path $OutDir "${name}_win32.zip"

if (-not (Test-Path (Join-Path $bin "BUSMASTER.exe"))) { throw "No build found in $bin" }
New-Item -ItemType Directory -Force $OutDir | Out-Null
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }

# 1. the build, without build by-products
Write-Host "Copying the build"
robocopy $bin $stage /E /NJH /NJS /NDL /NFL /NP /XF *.pdb *.ilk *.exp *.lib *.pch *.iobj *.ipdb *.recipe *.log *.tlog *.chw *.idb *.obj | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE)" }

# 2. C++ / MFC runtime of the toolset and Universal CRT, next to the executable (needed on Windows 7)
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
$redist = Join-Path $vs "VC\Redist\MSVC\$VCToolsVersion\x86"
foreach ($sub in "Microsoft.VC143.CRT", "Microsoft.VC143.MFC") {
    $dir = Join-Path $redist $sub
    if (-not (Test-Path $dir)) { throw "Runtime not found: $dir (install the MSVC $VCToolsVersion build tools)" }
    Copy-Item "$dir\*.dll" $stage
}
$ucrt = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\Redist\*\ucrt\DLLs\x86" -Directory | Sort-Object FullName | Select-Object -Last 1
if ($null -eq $ucrt) { throw "Universal CRT redistributable not found (Windows 10 SDK)" }
Copy-Item "$($ucrt.FullName)\*.dll" $stage

# 3. node simulation: makefile template, headers and helper libraries
Write-Host "Node simulation files"
Copy-Item (Join-Path $app "GCCDLLMakeTemplate") $stage
$simInc = Join-Path $stage "SimulatedSystems\include"
$simObj = Join-Path $stage "SimulatedSystems\OBJ"
New-Item -ItemType Directory -Force $simInc, $simObj | Out-Null
Copy-Item (Join-Path $app "SimulatedSystems\include\*.h") $simInc
$libSrc = Join-Path $app "SimulatedSystems\OBJ"
if ($MinGW -ne "") {
    $gcc = Join-Path $MinGW "bin"
    if (-not (Test-Path (Join-Path $gcc "g++.exe"))) { throw "g++.exe not found in $gcc" }
    New-Item -ItemType Directory -Force $libSrc | Out-Null
    $inc = Join-Path $app "SimulatedSystems\include"
    foreach ($bus in "CAN", "J1939", "LIN") {
        Write-Host "  building libWrapper_$bus.a"
        $lib = Join-Path $libSrc "libWrapper_$bus.a"
        if (Test-Path $lib) { Remove-Item $lib }
        # "call .\" is needed: a bare batch name is not found when cmd is started from PowerShell with this PATH
        # (stderr merged into stdout inside cmd: the scripts first delete files that may not exist yet)
        cmd /c "set PATH=$gcc;%PATH% && cd /d `"$inc`" && call .\MakeFile_$bus.bat 2>&1" | Out-Null
        if (-not (Test-Path $lib)) { throw "libWrapper_$bus.a was not built" }
    }
}
$libs = Get-ChildItem $libSrc -Filter "libWrapper_*.a" -ErrorAction SilentlyContinue
if ($libs.Count -eq 3) { Copy-Item $libs.FullName $simObj }
else { Write-Warning "Node simulation libraries missing in ${libSrc}: run again with -MinGW <folder>" }

# 4. other files of the original installer
Copy-Item (Join-Path $root "Sources\BUSMASTER\CAN_MHS\EXTERNAL\mhstcan.dll") $stage
Copy-Item (Join-Path $root "Examples") $stage -Recurse
foreach ($file in "COPYING.LESSER.txt", "COPYING.txt", "DBManager_License.txt", "CHANGELOG.md") {
    Copy-Item (Join-Path $root $file) $stage
}
# the commit list of the original BUSMASTER up to v3.2.x would be mistaken for the changes of this version
Remove-Item (Join-Path $stage "Changelog.txt") -ErrorAction SilentlyContinue
(Get-Content (Join-Path $PSScriptRoot "release-readme.txt") -Raw).Replace("{VERSION}", $Version) |
    Set-Content (Join-Path $stage "README.txt") -Encoding ASCII

# 5. zip and checksum
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path $stage -DestinationPath $zip -CompressionLevel Optimal
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash
Write-Host ("Package: {0} ({1:N1} MB)" -f $zip, ((Get-Item $zip).Length / 1MB))
Write-Host "SHA-256: $hash"
