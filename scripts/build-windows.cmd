@echo off
setlocal EnableExtensions
set "REPO_ROOT=%~dp0.."
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=DEBUG"
if /I "%CONFIG%"=="DEBUG" goto config_ok
if /I "%CONFIG%"=="RELEASE" goto config_ok
echo Usage: build-windows.cmd [DEBUG^|RELEASE]
exit /b 2

:config_ok
where cl.exe >nul 2>&1
if errorlevel 1 (
  echo ERROR: Open an x64 Native Tools Command Prompt for VS 2022.
  exit /b 1
)
where nasm.exe >nul 2>&1
if errorlevel 1 (
  echo ERROR: nasm.exe is not on PATH.
  exit /b 1
)
where iasl.exe >nul 2>&1
if errorlevel 1 (
  echo ERROR: iasl.exe is not on PATH.
  exit /b 1
)
if not defined NASM_PREFIX (
  for /f "delims=" %%I in ('where nasm.exe') do if not defined NASM_PREFIX for %%F in ("%%I") do set "NASM_PREFIX=%%~dpF"
)
if not defined IASL_PREFIX (
  for /f "delims=" %%I in ('where iasl.exe') do if not defined IASL_PREFIX for %%F in ("%%I") do set "IASL_PREFIX=%%~dpF"
)

git -C "%REPO_ROOT%" submodule update --init --recursive
if errorlevel 1 exit /b 1
if not exist "%REPO_ROOT%\.venv\Scripts\python.exe" (
  python -m venv "%REPO_ROOT%\.venv"
  if errorlevel 1 exit /b 1
)
call "%REPO_ROOT%\.venv\Scripts\activate.bat"
if errorlevel 1 exit /b 1
pushd "%REPO_ROOT%\edk2"
if errorlevel 1 exit /b 1
python -m pip install --disable-pip-version-check -r "%REPO_ROOT%\scripts\pip-requirements-ovmf.txt"
if errorlevel 1 goto fail
call edksetup.bat Rebuild
if errorlevel 1 goto fail
if not exist "%REPO_ROOT%\artifacts" mkdir "%REPO_ROOT%\artifacts"
build -p OvmfPkg/OvmfPkgX64.dsc -a X64 -t VS2022 -b %CONFIG% -n 4 -y "%REPO_ROOT%\artifacts\windows-%CONFIG%-build-report.txt" -Y PCD -Y FLASH
if errorlevel 1 goto fail
if not exist "Build\OvmfX64\%CONFIG%_VS2022\FV\OVMF.fd" (
  echo ERROR: build returned success but OVMF.fd was not found.
  goto fail
)
echo SUCCESS: %CD%\Build\OvmfX64\%CONFIG%_VS2022\FV\OVMF.fd
popd
exit /b 0

:fail
echo ERROR: EDK2 OVMF build failed.
popd
exit /b 1
