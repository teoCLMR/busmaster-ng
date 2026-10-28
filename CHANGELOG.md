# Changelog of BUSMASTER-NG

All user-visible changes of BUSMASTER-NG, newest first. Numbers like #2 are issues of
[teoCLMR/busmaster-ng](https://github.com/teoCLMR/busmaster-ng/issues); "original issue 1213" refers to
[rbei-etas/busmaster](https://github.com/rbei-etas/busmaster/issues).

The history of the original BUSMASTER up to v3.2.x is in `ReleaseNotes.txt` and `Changelog.txt` (ETAS, not changed).

## Unreleased (3.4.0)

Changes marked *(not tested yet)* are built but still waiting for the test listed in their issue.

**Databases**
- Message window and Signal Watch show the value descriptions of CAN signals again ("Position control" instead of
  3) (#1).
- CAN database editor (#2):
  - factor and offset are kept with full precision: a factor such as 0.0000001 is no longer saved as 0, and
    editing a signal no longer cuts the offset to 2 decimals (original issues 932, 1248, 1308);
  - Signal Details: physical minimum and maximum can be typed with decimals; the raw range stored in the database
    follows (1296, 215);
  - no false "Maximum value cannot be less than minimum value" for unsigned hexadecimal values (1213);
  - the position of the signal (LSB, MSB and the start bit of a DBC file) is shown, and the Motorola error message
    explains the convention (1315);
  - Save As keeps a path with a dot in a folder name (1210);
  - the editor opens big enough, the message tree is visible and the window can be resized (1337).

**Node simulation**
- Works with a current MinGW (tested with WinLibs GCC 16.2.0, 32 bit); BUSMASTER-NG finds a `MinGW` folder next to
  `BUSMASTER.exe` and says clearly what is missing (#4). See the wiki page *Node simulation*.

**Configuration**
- Open and Save dialogs remember the last folder for each kind of file (configuration, DBC, database, log, nodes,
  ...) instead of one folder for everything *(not tested yet, #3)*.
- The LIN driver and the visibility of the status bar are saved in the configuration (original issues 724, 1020,
  546) *(not tested yet, #3)*.
- No "Do you want to save?" on exit when nothing was changed (1004) *(not tested yet, #3)*.

**User interface**
- Replay window and node simulation output: readable font and size at high display scale.
- Start-up message boxes say BUSMASTER-NG.

**Drivers**
- The Ixxat driver is built again (Ixxat VCI V4 SDK); not tested with an Ixxat interface.

**Package**
- The zip contains the license texts, the node simulation files and this changelog.

## 3.3.0 "Overdrive" - 2026-10-26

First release of BUSMASTER-NG, the community fork of BUSMASTER 3.2.2.

**DBC support**
- **Load DBC** (CAN tab, or Ctrl+Shift+M): DBC databases are converted and associated directly, several at once
  (original issue 586).
- DBC ↔ DBF converters fixed: decimal numbers on Italian / German Windows, large offsets and very small factors
  kept exactly, Motorola and 64-bit signals, value descriptions, files that hung or crashed the converter or came
  out empty. 26 issues of the original project fixed, partly fixed or explained: see
  [docs/dbc-support.md](docs/dbc-support.md).
- The **Format Converter** tool is back (it no longer compiled).

**User interface**
- Sharp display at 125–200% scale (DPI aware): splash, icons, message window, Transmit window.
- Transmit window: resizable with controls that follow it, readable check boxes, negative physical values.
- Dissociate Database: resizable, long paths readable.
- New splash screen, icon and About dialog.

**Reliability**
- A missing or damaged driver DLL no longer makes the program hang at start-up: a message says which file is
  missing and why.
- The trace window shows the right text.

**Under the hood**
- Built with Visual Studio 2022; a Windows 7 compatible toolset is used for the package.
- Configurations (`.cfx`) of BUSMASTER 3.2.x open unchanged.

**Known limitations**
- The database engine `DBManager.dll` is a closed binary of the original project: factor/offset with about 7
  significant digits, IEEE float signals shown as raw values, no CAN FD messages in databases, multiplexed signals
  always all shown.
- Japanese resources are not maintained; the help still has images of the original BUSMASTER.
