# DBC support in BUSMASTER-NG

BUSMASTER works with its own database format (`.dbf`). Most CAN networks are described in the DBC format of
Vector CANdb++, so with BUSMASTER 3.2.2 a DBC had to be converted first with *Tools → Format Converter*, and the
converter made mistakes. BUSMASTER-NG loads DBC files directly and fixes the converters.

## Loading a DBC

*CAN → Load DBC* (next to *Database*), or **Ctrl+Shift+M**:

1. choose one or more `.dbc` files;
2. each DBC is converted to a BUSMASTER database with the same name **next to the DBC**
   (`Motor.dbc` → `Motor.dbf`). If that folder cannot be written (for example a read-only network share), the
   database goes to `%LOCALAPPDATA%\BUSMASTER-NG\DBC`;
3. the database is associated, as with *Database → Associate*.

The trace window lists what happened, including the warnings of the conversion (also written to `<name>.log`
next to the database).

An existing `.dbf` with the same name is never replaced without asking, because it may have been changed in the
database editor: BUSMASTER-NG asks whether to replace it with a new conversion, to associate it as it is, or to
skip the DBC. A database created this way starts with the line `//Converted from Motor.dbc by BUSMASTER-NG`, so the
question can say whether the file comes from that DBC and whether the DBC has been changed since then.

The configuration stores the path of the `.dbf`, so configurations stay readable by BUSMASTER 3.2.x. After the DBC
changes, load it again.

## What was wrong with the converters, and the upstream issues

Issues of [rbei-etas/busmaster](https://github.com/rbei-etas/busmaster/issues) about the conversion between DBC and
DBF, and what BUSMASTER-NG does about them:

| Issue | Problem | Status in BUSMASTER-NG |
|---|---|---|
| [#586](https://github.com/rbei-etas/busmaster/issues/586) | No direct DBC support | **Done**: *Load DBC* |
| [#498](https://github.com/rbei-etas/busmaster/issues/498), [#499](https://github.com/rbei-etas/busmaster/issues/499), [#570](https://github.com/rbei-etas/busmaster/issues/570), [#378](https://github.com/rbei-etas/busmaster/issues/378) | Factor, offset, min and max wrong or cut at the decimal point; depended on the language of Windows | **Fixed**: numbers are always read and written with `.`, the result is the same on every PC |
| [#932](https://github.com/rbei-etas/busmaster/issues/932), [#1248](https://github.com/rbei-etas/busmaster/issues/1248), [#1308](https://github.com/rbei-etas/busmaster/issues/1308) | Factors below 0.000001 (GPS 1e-7) lost | **Fixed in the converters** (all digits are kept). The database editor dialog still shows 6 decimals |
| [#174](https://github.com/rbei-etas/busmaster/issues/174), [#980](https://github.com/rbei-etas/busmaster/issues/980), [#838](https://github.com/rbei-etas/busmaster/issues/838) | Conversion "successful" but the database is empty, or the converter locks up, when the `NS_` list is indented with spaces | **Fixed**; a DBC without any usable message is now reported as an error |
| [#1105](https://github.com/rbei-etas/busmaster/issues/1105), [#1183](https://github.com/rbei-etas/busmaster/issues/1183), [#1293](https://github.com/rbei-etas/busmaster/issues/1293), PR [#1184](https://github.com/rbei-etas/busmaster/pull/1184) | Converter crashes on some (large) DBC files | **Fixed**: lines of any length, empty lines, long value descriptions and multi-line `VAL_` lists no longer overflow buffers |
| [#914](https://github.com/rbei-etas/busmaster/issues/914) | J1939: factor 0.125 became 0 | **Fixed** (same converter) |
| [#1178](https://github.com/rbei-etas/busmaster/issues/1178) | DBF → DBC: value descriptions wrong (`0 "German,4"`) | **Fixed**, with or without quotes in the DBF, in the original order |
| [#1269](https://github.com/rbei-etas/busmaster/issues/1269) | DBF → DBC: 1-bit signals written as signed | **Fixed**: written as unsigned |
| [#1038](https://github.com/rbei-etas/busmaster/issues/1038) | DBF → DBC: wrong start bit of Motorola signals | **Fixed** (the example of the issue now gives `18\|5@0+`) |
| [#1164](https://github.com/rbei-etas/busmaster/issues/1164) | A DBF converted from a DBC cannot be converted back | **Fixed**: DBC → DBF → DBC → DBF gives the same signals; lines over 1025 characters and comments closed by `" ;` no longer stop or hang the conversion |
| [#510](https://github.com/rbei-etas/busmaster/issues/510) | Convert several files at once | **Done for DBC**: *Load DBC* accepts several files |
| [#561](https://github.com/rbei-etas/busmaster/issues/561) | Description of the DBF format | See *The DBF format in brief* below |
| [#1075](https://github.com/rbei-etas/busmaster/issues/1075), [#1133](https://github.com/rbei-etas/busmaster/issues/1133), [#1171](https://github.com/rbei-etas/busmaster/issues/1171), [#1025](https://github.com/rbei-etas/busmaster/issues/1025) | Value descriptions not shown in the Message window since BUSMASTER 3.0 | **Fixed for CAN** (after 3.3.0): the database engine does not return them, so BUSMASTER-NG reads them from the `.dbf` itself; Message window and Signal Watch show the text. J1939 and LIN still show numbers |
| [#1155](https://github.com/rbei-etas/busmaster/issues/1155) | Multiplexed signals | **Partly**: the converter keeps the multiplexing (`M`, `m0`, ...) in both directions, but BUSMASTER shows every multiplexed signal, whatever the multiplexer value (database engine, see below) |
| [#1039](https://github.com/rbei-etas/busmaster/issues/1039), [#1176](https://github.com/rbei-etas/busmaster/issues/1176), [#874](https://github.com/rbei-etas/busmaster/issues/874) | IEEE float / double signals | **Partly**: they are kept (`SIG_VALTYPE_` in both directions) and listed in the conversion log, but BUSMASTER shows their raw integer value (database engine) |
| [#1104](https://github.com/rbei-etas/busmaster/issues/1104) | Extended id differs from another tool | **Not a converter bug**: the id is taken from the DBC as it is; the other tool adds GMLAN-specific bits |

Other fixes found while testing:

- 64-bit unsigned signals: their range was halved (DBC → DBF), and a maximum above 9223372036854775807 made the
  database engine refuse the **whole** database. They are written the way the database editor writes them.
- Message ids with a leading zero were read as octal numbers.
- `BA_DEF_` and `BA_DEF_DEF_` with one space after the keyword (as many tools write them) were read wrongly
  ("Match not Found in Param List"); FLOAT attributes with a range starting at 0 or below were reported as errors.
- CAN FD messages (more than 8 data bytes) are listed in the log as skipped, and their signals no longer become
  "signals not associated with any message".
- DBF → DBC keeps the order of signals and value descriptions, and writes clean numbers (`250`, not `2.5e+02`).

## Limits of the database engine

The database files are read by `DBManager.dll`, a closed binary of the original project that cannot be changed.
It:

- keeps factor and offset with about **7 significant digits** (an offset of -490.219 is used as -490.21899...,
  -2105540607 as -2105540608); the DBF itself keeps every digit;
- shows **IEEE float/double** signals as their raw integer value;
- refuses the **whole database** if one message has more than 8 data bytes (**CAN FD**). BUSMASTER does support CAN
  FD on the bus with interfaces that can do it (for example ETAS BOA 2, Vector XL): such frames are sent and
  received, but shown only as raw bytes, without signals. This is why the converter leaves CAN FD messages out;
- shows **every multiplexed signal**, whatever the value of the multiplexer;
- reads numbers in exponent notation (`1e-07`) wrongly, so the converter never writes them;
- does not return the **value descriptions** of signals (`[VALUE_DESCRIPTION]`). BUSMASTER-NG reads them from the
  `.dbf` itself for CAN (`Utility/DbValueDescriptions.h`), and reads the file again when it changes.

## How the converters were tested

Every DBC was converted, the resulting DBF was loaded by the real `DBManager.dll`, and random and edge payloads were
decoded by it and compared with an independent decoder that reads the DBC directly (Intel and Motorola bit order,
signed values, factor, offset, float types). The same check was repeated after DBC → DBF → DBC → DBF and with the
Italian language settings active. Test files: a real customer DBC with large offsets and factors such as
0.015258789063, and synthetic DBC files covering Motorola signals, 64-bit signals, 1e-7 factors, multiplexing,
float types, CAN FD, tabs, long and multi-line value tables and comments, and the formatting variants above.

## The DBF format in brief

```
[START_MSG] <name>,<id>,<data bytes>,<number of signals>,<byte order 1=Intel 0=Motorola>,<S=standard X=extended>,<tx node>
[START_SIGNALS] <name>,<length in bits>,<byte 1..8>,<bit 0..7>,<type>,<max raw>,<min raw>,<byte order>,<offset>,<factor>,<unit>,<multiplexing>,<rx nodes>
[VALUE_DESCRIPTION] "<text>",<raw value>
[END_MSG]
```

- `<byte>` and `<bit>` locate the **least significant bit** of the signal, for both byte orders.
- `<type>`: `B` bit, `U` unsigned, `I` signed, `F` IEEE float, `D` IEEE double. Signals longer than 32 bits use `U`
  or `I` as well.
- `<max raw>` and `<min raw>` are raw values (before factor and offset); for 64-bit unsigned signals they are
  written as signed 64-bit numbers (`-1` means all bits set).
- `<multiplexing>`: empty, `M` (multiplexer) or `m<value>` (multiplexed signal), as in the DBC.
- Numbers always use `.` as decimal symbol and no exponent.

The file also holds value tables, node list, comments, attributes and the DBC lines that BUSMASTER does not use
(`[START_NOT_PROCESSED]`), so that a DBF can be converted back to DBC.
