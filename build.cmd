@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

set "ROOT=%~dp0"

rem ---- Auto-locate node/npm ----
rem When launched by double-clicking this .cmd, node may only exist in WorkBuddy's
rem injected PATH (not in the system/user environment), so npm is missing and the
rem script fails silently. Fall back to WorkBuddy's bundled node directory.
set "NPM_OK=0"
where npm >nul 2>nul && set "NPM_OK=1"
if "%NPM_OK%"=="0" (
  set "NODE_DIR="
  for /d %%d in ("%USERPROFILE%\.workbuddy\binaries\node\versions\*") do (
    if exist "%%d\node.exe" if not defined NODE_DIR set "NODE_DIR=%%d"
  )
  if defined NODE_DIR (
    set "PATH=!NODE_DIR!;%PATH%"
    where npm >nul 2>nul && set "NPM_OK=1"
  )
)
if "%NPM_OK%"=="0" (
  echo [ERROR] npm not found. Please install Node.js or verify the WorkBuddy node directory.
  pause
  exit /b 1
)

set "VSDIR=C:\Program Files\Microsoft Visual Studio\18\Community"
set "VCVARS=%VSDIR%\VC\Auxiliary\Build\vcvars64.bat"
set "CMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "NINJA=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"

rem Windows SDK headers/libs come from the NuGet SDK packages (no full SDK on this box)
set "SDK_INC=%ROOT%third_party\winsdk\microsoft.windows.sdk.cpp\c\Include\10.0.26100.0"
set "SDK_LIB=%ROOT%third_party\winsdk\microsoft.windows.sdk.cpp.x64\c"

echo [1/4] build web frontend...
pushd web
if not exist node_modules (
    call npm install --no-audit --no-fund || goto :fail
)
call npm run build || goto :fail
popd

echo [2/4] setup MSVC environment...
call "%VCVARS%" >nul || goto :fail
set "INCLUDE=%SDK_INC%\ucrt;%SDK_INC%\shared;%SDK_INC%\um;%SDK_INC%\winrt;%INCLUDE%"
set "LIB=%SDK_LIB%\ucrt\x64;%SDK_LIB%\um\x64;%LIB%"
set "PATH=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%ROOT%third_party\winsdk\buildtools\bin\10.0.26100.0\x64;%PATH%"

echo [3/4] configure...
if not exist build mkdir build
pushd build
"%CMAKE%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM="%NINJA%" .. || goto :fail_pop
echo [4/4] compile...
"%CMAKE%" --build . || goto :fail_pop
popd

echo.
echo done: %ROOT%build\Vicold.MIHome.exe
echo.
echo Build complete. Press any key to close...
pause >nul
exit /b 0

:fail_pop
popd
:fail
echo.
echo [ERROR] build failed, see output above.
echo Press any key to close...
pause >nul
exit /b 1
