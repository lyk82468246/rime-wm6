@echo off
setlocal EnableDelayedExpansion

rem ============================================================
rem pack.bat -- Build Release + assemble dist\ + produce CAB.
rem
rem Outputs:
rem   dist\WMRime.CAB         (~1.8 MB, deploy to device)
rem   dist\WMRime.err         (cabwiz log; a couple of "empty
rem                           default value" warnings are benign)
rem
rem Idempotent: re-run to refresh the CAB after code/dict changes.
rem ============================================================

set ROOT=%~dp0
set VS_TOOLS="C:\Program Files (x86)\Microsoft Visual Studio 9.0\VC\vcvarsall.bat"
set CABWIZ="C:\Program Files (x86)\Microsoft Visual Studio 9.0\SmartDevices\SDK\SDKTools\cabwiz.exe"
set CFG="Release|Windows Mobile 6 Professional SDK (ARMV4I)"

rem Build output dirs contain parens, so always quote when expanding.
set "OUTDIR=Windows Mobile 6 Professional SDK (ARMV4I)\Release"
set "RIMECORE_DLL=%ROOT%rime-wm6\RimeCore\%OUTDIR%\RimeCore.dll"
set "WMRIMESIP_DLL=%ROOT%rime-wm6\WMRimeSIP\%OUTDIR%\WMRimeSIP.dll"
set "PRISM_BIN=%ROOT%data\luna_pinyin.prism.bin"
set "TABLE_BIN=%ROOT%data\luna_pinyin.table.bin"
set "DIST=%ROOT%dist"

echo === [1/4] Building Release configuration ===
call %VS_TOOLS% >nul
if errorlevel 1 (
    echo FAILED to set up VS2008 environment.
    exit /b 1
)
devenv "%ROOT%rime-wm6\rime-wm6.sln" /Build %CFG%
if errorlevel 1 (
    echo BUILD FAILED. Aborting.
    exit /b 1
)

echo.
echo === [2/4] Verifying build outputs ===
if not exist "!RIMECORE_DLL!" goto :missing_rimecore
if not exist "!WMRIMESIP_DLL!" goto :missing_sip
if not exist "!PRISM_BIN!" goto :missing_prism
if not exist "!TABLE_BIN!" goto :missing_table

echo.
echo === [3/4] Assembling dist\ ===
if not exist "!DIST!" mkdir "!DIST!"
copy /Y "!RIMECORE_DLL!"  "!DIST!\RimeCore.dll"           >nul || exit /b 1
copy /Y "!WMRIMESIP_DLL!" "!DIST!\WMRimeSIP.dll"          >nul || exit /b 1
copy /Y "!PRISM_BIN!"     "!DIST!\luna_pinyin.prism.bin"  >nul || exit /b 1
copy /Y "!TABLE_BIN!"     "!DIST!\luna_pinyin.table.bin"  >nul || exit /b 1

if not exist "!DIST!\WMRime.inf" goto :missing_inf

echo.
echo === [4/4] Running cabwiz ===
pushd "!DIST!"
%CABWIZ% WMRime.inf /compress /err WMRime.err
set CABWIZ_ERR=%errorlevel%
popd
if not %CABWIZ_ERR%==0 (
    echo cabwiz returned %CABWIZ_ERR%. See !DIST!\WMRime.err
    exit /b %CABWIZ_ERR%
)

echo.
echo === DONE ===
echo CAB: !DIST!\WMRime.CAB
for %%I in ("!DIST!\WMRime.CAB") do echo Size: %%~zI bytes
endlocal
exit /b 0

:missing_rimecore
echo MISSING: RimeCore.dll under rime-wm6\RimeCore\Release.
exit /b 1
:missing_sip
echo MISSING: WMRimeSIP.dll under rime-wm6\WMRimeSIP\Release.
exit /b 1
:missing_prism
echo MISSING: data\luna_pinyin.prism.bin -- run tools\build_dict.py first.
exit /b 1
:missing_table
echo MISSING: data\luna_pinyin.table.bin -- run tools\build_dict.py first.
exit /b 1
:missing_inf
echo MISSING: dist\WMRime.inf -- not in git? Restore from repo.
exit /b 1
