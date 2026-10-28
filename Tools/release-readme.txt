BUSMASTER-NG {VERSION}
======================

Community fork of BUSMASTER (rbei-etas/busmaster), LGPL v3 or later (see COPYING.LESSER.txt and COPYING.txt).
Not affiliated with ETAS GmbH or Robert Bosch GmbH.
https://github.com/teoCLMR/busmaster-ng          User guide: https://github.com/teoCLMR/busmaster-ng/wiki

Install
-------
No setup needed: extract the folder anywhere and start BUSMASTER.exe.

Requirements
- Windows 10 or 11 (32 or 64 bit). Windows 7 SP1 should work but is not tested yet.
- Microsoft Visual C++ 2013 Redistributable (x86), from microsoft.com. Some components inherited from
  BUSMASTER (DBManager.dll, libxml2.dll, the PEAK driver) need it. Most PCs already have it.
  The newer C++ and MFC runtimes are already in this folder.
- The driver of your CAN / LIN interface (PEAK, Vector, Kvaser, ETAS, ...), installed separately.
- Node simulation only: MinGW (GCC for Windows, 32 bit), see the wiki page "Node simulation".

What's new
----------
CHANGELOG.md                      Changes of every BUSMASTER-NG version (a text file, open it with Notepad)

Licenses
--------
COPYING.LESSER.txt, COPYING.txt   GNU LGPL v3 and GNU GPL v3 (BUSMASTER and BUSMASTER-NG)
DBManager_License.txt             License of DBManager.dll, distributed unmodified in binary form

Problems
--------
Please open an issue on GitHub (English or Italian): https://github.com/teoCLMR/busmaster-ng/issues
