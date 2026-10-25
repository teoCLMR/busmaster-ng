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
- The vendor driver of your CAN / LIN interface (PEAK, Vector, Kvaser, ETAS, ...), installed separately.

## Building

Install Visual Studio 2022 (Community is enough) with:

- the *Desktop development with C++* workload,
- *C++ MFC for latest v143 build tools (x86 & x64)*.

Then run `Sources\build.bat`, or open `Sources\Kernel\BusmasterKernel.sln` and
`Sources\BUSMASTER\BUSMASTER.sln` in Visual Studio and build the *Release* configuration.
The output is written to `Sources\BUSMASTER\BIN\Release`.

### Windows 7 build

The Visual Studio 2022 runtime from toolset 14.40 on needs Windows 8 or later. For Windows 7 build with the
older 14.38 toolset (Visual Studio 2022 17.8), selected by a switch in `Sources\Directory.Build.props`:

```
MSBuild Sources\BUSMASTER\BUSMASTER.sln /p:Configuration=Release /p:BusmasterWin7=true
```

This needs the *MSVC v143 ... (v14.38-17.8)* component plus the matching *C++ ATL* and *C++ MFC* components, and the
14.38 Visual C++ runtime next to the executable (or installed) on the target PC. The output goes to the same
`BIN\Release` folder, so do a clean build before packaging. The Windows 7 build has not been tested on a real
Windows 7 machine yet.

Known build notes:

- `CAN_IXXAT_VCI` needs the IXXAT VCI SDK from the vendor (`vcinpl.h`); without it that driver is skipped.
- The post-build step of `BusEmulation` registers a COM server and needs an elevated (administrator) prompt.
- LDF Editor and LDF Viewer additionally need Qt 5.

## License

BUSMASTER-NG is distributed under the **GNU Lesser General Public License, version 3 or later**,
like the original project. See `COPYING.LESSER.txt` and `COPYING.txt`. Third-party components keep their
own licenses; see `Readme.txt`.
