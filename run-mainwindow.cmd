@echo off
setlocal

set "QT_ROOT=C:\Qt\5.9.9\mingw53_32"
set "MINGW_ROOT=C:\Qt\Tools\mingw530_32"
set "CMAKE=C:\Qt\Tools\CMake_64\bin\cmake.exe"
set "NINJA=C:\Qt\Tools\Ninja\ninja.exe"
set "WINDEPLOYQT=%QT_ROOT%\bin\windeployqt.exe"
set "BUILD_DIR=%~dp0build\mainwindow"
set "PATH=%QT_ROOT%\bin;%MINGW_ROOT%\bin;C:\Qt\Tools\Ninja;%PATH%"

"%CMAKE%" -S "%~dp0." -B "%BUILD_DIR%" -G Ninja ^
  -DBUILD_MAINWINDOW_APP=ON ^
  -DBUILD_TESTING=OFF ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_PREFIX_PATH="%QT_ROOT%" ^
  -DCMAKE_CXX_COMPILER="%MINGW_ROOT%\bin\g++.exe" ^
  -DCMAKE_MAKE_PROGRAM="%NINJA%"
if errorlevel 1 goto :error

"%CMAKE%" --build "%BUILD_DIR%" --target mainwindow-app
if errorlevel 1 goto :error

if /I "%~1"=="--deploy" goto :deploy
if not exist "%BUILD_DIR%\app\Qt5Core.dll" goto :deploy
if not exist "%BUILD_DIR%\app\Qt5Gui.dll" goto :deploy
if not exist "%BUILD_DIR%\app\Qt5Widgets.dll" goto :deploy
if not exist "%BUILD_DIR%\app\libgcc_s_dw2-1.dll" goto :deploy
if not exist "%BUILD_DIR%\app\libstdc++-6.dll" goto :deploy
if not exist "%BUILD_DIR%\app\libwinpthread-1.dll" goto :deploy
if not exist "%BUILD_DIR%\app\platforms\qwindows.dll" goto :deploy
goto :launch

:deploy
"%WINDEPLOYQT%" --release --compiler-runtime --no-translations ^
  "%BUILD_DIR%\app\mainwindow-app.exe"
if errorlevel 1 goto :error

:launch
start "" "%BUILD_DIR%\app\mainwindow-app.exe"
exit /b 0

:error
echo MainWindow App build failed.
pause
exit /b 1
