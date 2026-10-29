BUSMASTER-NG {VERSION}  ({EDITION})
======================

Community fork of BUSMASTER (rbei-etas/busmaster), LGPL v3 or later (see Docs\COPYING.LESSER.txt and Docs\COPYING.txt).
Not affiliated with ETAS GmbH or Robert Bosch GmbH.
https://github.com/teoCLMR/busmaster-ng          User guide: https://github.com/teoCLMR/busmaster-ng/wiki

Install
-------
No setup needed: extract the folder anywhere and start BUSMASTER.exe.
Each release has two portable packages, with the same program: "_win32.zip" for Windows 10 / 11 and
"_win32_win7.zip" for Windows 7 SP1 (it adds the Universal C runtime, which Windows 10 and later already have).
Some releases also have an installer ("_setup.exe") that works on all of them and adds the runtimes it needs.

Requirements
- Windows 10 or 11 (32 or 64 bit), or Windows 7 SP1 with the "_win7" package (tested on a clean Windows 7 PC for
  start-up and the Simulation driver only so far).
- Microsoft Visual C++ 2013 Redistributable (x86), from microsoft.com. Some components inherited from
  BUSMASTER (DBManager.dll, libxml2.dll, the PEAK driver) need it. Most PCs already have it.
  The newer C++ and MFC runtimes are already in this folder.
- The driver of your CAN / LIN interface (PEAK, Vector, Kvaser, ETAS, ...), installed separately.
- Node simulation only: MinGW (GCC for Windows, 32 bit), see the wiki page "Node simulation".

What's new
----------
Docs\CHANGELOG.md                       Changes of every BUSMASTER-NG version (a text file, open it with Notepad)

Licenses
--------
Docs\COPYING.LESSER.txt, COPYING.txt    GNU LGPL v3 and GNU GPL v3 (BUSMASTER and BUSMASTER-NG)
Docs\DBManager_License.txt              License of DBManager.dll, distributed unmodified in binary form
Docs\THIRD-PARTY-NOTICES.txt            Licenses of the other components (libxml2, zlib, DMGraph, ...) and the
                                        interface vendor libraries in this folder

Problems
--------
Please open an issue on GitHub (English or Italian): https://github.com/teoCLMR/busmaster-ng/issues
