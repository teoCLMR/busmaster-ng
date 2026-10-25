# BUSMASTER-NG

**BUSMASTER-NG** (*Next Generation*, code name **Overdrive**) is a community-maintained fork of
[BUSMASTER](https://github.com/rbei-etas/busmaster), the open source tool for monitoring, analysing and
simulating CAN, LIN and FlexRay bus systems.

The original project has not been updated since 2017 (last release 3.2.2). BUSMASTER-NG keeps it alive:
modern build toolchain, bug fixes and maintenance.

> BUSMASTER-NG is an independent community project. It is **not affiliated with, endorsed by or
> supported by ETAS GmbH or Robert Bosch GmbH**. BUSMASTER is the work of its original authors; their
> copyright notices are preserved in the source files.

## Maintainer

- Developer name: **bigt92**
- GitHub: **teoCLMR**

## What is different from BUSMASTER 3.2.2

- Version **3.3.0**, product name BUSMASTER-NG, new splash screen, icon and installer artwork
  (the ETAS / Bosch logos are no longer used).
- Built with **Visual Studio 2022** (MSVC v143 toolset) and the Windows 10 SDK instead of Visual Studio 2013.
- Windows XP support removed.
- User data and settings are kept apart from earlier versions (folder and registry key of version 3.3.0),
  so BUSMASTER-NG can be installed next to BUSMASTER 3.2.2.
- The About dialog points to the original project and credits its authors.

Configuration files created by BUSMASTER 3.2.x are **not yet verified** to open correctly in NG.

## Requirements

- Windows 10 or later for the default build. Windows 7 is not supported by the current Visual C++ 2022
  runtime; a separate Windows 7 build is available (see *Windows 7 build* below).
- **Windows 7 SP1** (Windows 7 build only): the *Microsoft Visual C++ 2013 Redistributable (x86)* must be installed
  on the PC. The prebuilt components inherited from BUSMASTER (`libxml2.dll`, `DBManager.dll`, the PEAK driver
  `CAN_PEAK_USB.dll`, ...) are built with that runtime, and `BUSMASTER.exe` loads `libxml2.dll` at start-up.
  The C++ / MFC 14.38 runtime and the Universal CRT are delivered next to the executable (see below).
- The vendor driver of your CAN / LIN interface (PEAK, Vector, Kvaser, ETAS, ...), installed separately.

## Building

Install Visual Studio 2022 (Community is enough) with:

- the *Desktop development with C++* workload,
- *C++ MFC for latest v143 build tools (x86 & x64)*.

Then run `Sources\build.bat`, or open `Sources\Kernel\BusmasterKernel.sln` and
`Sources\BUSMASTER\BUSMASTER.sln` in Visual Studio and build the *Release* configuration.
The output is written to `Sources\BUSMASTER\BIN\Release`.

### Windows 7 build

The Visual Studio 2022 runtime from toolset 14.40 on needs Windows 8 or later (for example the C++ standard library
imports `GetSystemTimePreciseAsFileTime` and `CreateFile2`). For Windows 7 build with the older 14.38 toolset
(Visual Studio 2022 17.8), selected by a switch in `Sources\Directory.Build.props`.

Requirements on the build PC: the Visual Studio components *MSVC v143 ... (v14.38-17.8)* and the matching
*C++ ATL* and *C++ MFC* (v14.38) build tools.

1. Clean build with the switch (the toolset changes, so objects of the default build cannot be reused):

   ```
   MSBuild Sources\Kernel\BusmasterKernel.sln       /t:Rebuild /p:Configuration=Release /p:BusmasterWin7=true
   MSBuild Sources\BUSMASTER\BUSMASTER.sln          /t:Rebuild /p:Configuration=Release /p:BusmasterWin7=true
   ```

   `BusEmulation` (COM registration needs an administrator prompt) and `CAN_IXXAT_VCI` (vendor SDK) may fail; if
   `BUSMASTER.exe` was skipped because of that, build the project alone with
   `/t:Build /p:BuildProjectReferences=false /p:SolutionDir=<full path of Sources\BUSMASTER with a trailing />`.
   A clean of the Application project deletes the tracked `BIN\Release\BUSMASTER.exe.manifest`; restore it with
   `git checkout -- Sources/BUSMASTER/BIN/Release/BUSMASTER.exe.manifest` before building the Application.

2. Assemble a self-contained folder (the output of step 1 is in `Sources\BUSMASTER\BIN\Release`). Copy that folder,
   without the intermediate files (`*.pch`, `*.ilk`, `*.pdb`, `*.exp`, `*.lib`), and add next to `BUSMASTER.exe`:
   - `msvcp140.dll`, `vcruntime140.dll`, `concrt140.dll` from `VC\Redist\MSVC\14.38.33130\x86\Microsoft.VC143.CRT`;
   - `mfc140.dll`, `mfc140u.dll`, `mfcm140.dll`, `mfcm140u.dll` from `...\Microsoft.VC143.MFC`;
   - all DLLs of `C:\Program Files (x86)\Windows Kits\10\Redist\ucrt\DLLs\x86` (the Universal CRT for
     application-local deployment, with `ucrtbase.dll` and the `api-ms-win-*` files).

3. On the Windows 7 PC install Windows 7 SP1 and the Visual C++ 2013 Redistributable (x86), then run the folder.

The result is checked with `dumpbin /imports`: none of the 36 compiled binaries, nor the 14.38 C++ / MFC runtime,
imports a function that is missing on Windows 7. It has **not been tested on a real Windows 7 machine yet**.
The default build and the Windows 7 build write to the same `BIN\Release` folder: do a clean build whenever you
switch between them.

Known build notes:

- `CAN_IXXAT_VCI` needs the IXXAT VCI SDK from the vendor (`vcinpl.h`); without it that driver is skipped.
- The post-build step of `BusEmulation` registers a COM server and needs an elevated (administrator) prompt.
- LDF Editor and LDF Viewer additionally need Qt 5.
- The PEAK driver (`CAN_PEAK_USB.dll`), `DBManager.dll`, `libxml2.dll` and the other vendor libraries in
  `BIN\Release` are prebuilt binaries inherited from BUSMASTER 3.2.2 (the PEAK driver has no sources in the
  repository), so they are not rebuilt with the new toolset.

## License

BUSMASTER-NG is distributed under the **GNU Lesser General Public License, version 3 or later**,
like the original project. See `COPYING.LESSER.txt` and `COPYING.txt`. Third-party components keep their
own licenses; see `Readme.txt`.
