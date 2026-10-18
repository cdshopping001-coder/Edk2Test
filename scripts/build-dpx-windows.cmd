@echo off
setlocal EnableExtensions
set "ROOT=%~dp0.."
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=RELEASE"
if /I "%CONFIG%"=="DEBUG" goto valid
if /I "%CONFIG%"=="RELEASE" goto valid
echo Usage: build-dpx-windows.cmd [DEBUG^|RELEASE]
exit /b 2
:valid
where cl.exe >nul 2>&1
if errorlevel 1 (echo ERROR: VS2022 compiler missing & exit /b 1)
where nasm.exe >nul 2>&1
if errorlevel 1 (echo ERROR: nasm.exe missing & exit /b 1)
where iasl.exe >nul 2>&1
if errorlevel 1 (echo ERROR: iasl.exe missing & exit /b 1)
git -C "%ROOT%" submodule update --init --recursive edk2
if errorlevel 1 exit /b 1
if not exist "%ROOT%\.venv\Scripts\python.exe" (
  python -m venv "%ROOT%\.venv"
  if errorlevel 1 exit /b 1
)
call "%ROOT%\.venv\Scripts\activate.bat"
if errorlevel 1 exit /b 1
set "WORKSPACE=%ROOT%\edk2"
set "PACKAGES_PATH=%ROOT%"
if not defined NASM_PREFIX (
  for /f "delims=" %%I in ('where nasm.exe') do if not defined NASM_PREFIX for %%F in ("%%I") do set "NASM_PREFIX=%%~dpF"
)
if not defined IASL_PREFIX (
  for /f "delims=" %%I in ('where iasl.exe') do if not defined IASL_PREFIX for %%F in ("%%I") do set "IASL_PREFIX=%%~dpF"
)
pushd "%WORKSPACE%"
if errorlevel 1 exit /b 1
python -m pip install --disable-pip-version-check -r "%ROOT%\scripts\pip-requirements-ovmf.txt"
if errorlevel 1 goto fail
call edksetup.bat Rebuild
if errorlevel 1 goto fail
build -p CommonToolsPkg/CommonToolsPkg.dsc -a X64 -t VS2022 -b %CONFIG% -n 4
if errorlevel 1 goto fail
set "OUTPUT="
for /r "Build\DpX" %%I in (DpX.efi) do if not defined OUTPUT set "OUTPUT=%%~fI"
if not defined OUTPUT (echo ERROR: DpX.efi missing & goto fail)
if not exist "%ROOT%\artifacts\dpx" mkdir "%ROOT%\artifacts\dpx"
copy /y "%OUTPUT%" "%ROOT%\artifacts\dpx\DpX-VS2022-X64-%CONFIG%.efi" >nul
if errorlevel 1 goto fail
certutil -hashfile "%ROOT%\artifacts\dpx\DpX-VS2022-X64-%CONFIG%.efi" SHA256
if errorlevel 1 goto fail
echo SUCCESS: %ROOT%\artifacts\dpx\DpX-VS2022-X64-%CONFIG%.efi
popd
exit /b 0
:fail
popd
echo ERROR: DpX build failed
exit /b 1
