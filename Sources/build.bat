@echo off

:MSBUILD_FIND
REM Locate the MSBuild of Visual Studio 2022 or Build Tools 2022 (v143 toolset) via vswhere.
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if not exist "%VSWHERE%" goto MSBUILD_NOT_FOUND
set MSBUILD_EXE=
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -version [17.0^,18.0^) -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find MSBuild\**\Bin\MSBuild.exe`) do set "MSBUILD_EXE=%%i"
if defined MSBUILD_EXE goto BUILD

:MSBUILD_NOT_FOUND
echo Visual Studio 2022 with C++ desktop tools (and MFC) not found. Build failed!
goto END

:BUILD
echo Using MSBuild found in %MSBUILD_EXE%
for %%i in ("%MSBUILD_EXE%") do set "PATH=%%~dpi;%PATH%"
MSBuild "Kernel\BusmasterKernel.sln" /property:Configuration=Release
MSBuild "BUSMASTER\BUSMASTER.sln" /property:Configuration=Release 

REM CAN PEAK USB.
MSBuild "BUSMASTER\CAN_PEAK_USB\CAN_PEAK_USB.vcxproj" /property:Configuration=Release  

MSBuild "BUSMASTER\Language Dlls\Language Dlls.sln" /property:Configuration=Release 
MSBuild "BUSMASTER\LDFEditor\LDFEditor.sln" /property:Configuration=Release 
MSBuild "BUSMASTER\LDFViewer\LDFViewer.sln" /property:Configuration=Release

REM Asc Log
cd ..\Tools\flex 
"flex.exe" -i -L -o"..\..\Sources\BUSMASTER\Format Converter\AscLogConverter\Asc_Log_Lexer.c" "..\..\Sources\BUSMASTER\Format Converter\AscLogConverter\Asc_Log_Lexer.l"
cd ..\bison
"bison.exe" -d -l -o"..\..\Sources\BUSMASTER\Format Converter\AscLogConverter\Asc_Log_Parser.c" "..\..\Sources\BUSMASTER\Format Converter\AscLogConverter\Asc_Log_Parser.y"

REM Log Asc
cd ..\flex 
"flex.exe" -i -L -o"..\..\Sources\BUSMASTER\Format Converter\LogAscConverter\Log_Asc_Lexer.c" "..\..\Sources\BUSMASTER\Format Converter\LogAscConverter\Log_Asc_Lexer.l"
cd ..\bison
"bison.exe" -d -l -o"..\..\Sources\BUSMASTER\Format Converter\LogAscConverter\Log_Asc_Parser.c" "..\..\Sources\BUSMASTER\Format Converter\LogAscConverter\Log_Asc_Parser.y"

cd ..\..\Sources
MSBuild "BUSMASTER\Format Converter\FormatConverter.sln" /property:Configuration=Release 
:END

