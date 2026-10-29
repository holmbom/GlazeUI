@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion
title GlazeUI VS Code-extension - installer

rem ============================================================
rem  Installerar GlazeUI-extensionen for VS Code pa Windows.
rem  Kor i extensionens egen mapp (tools\vscode-glazeui\).
rem  Steg: Node.js-koll -> npm install -> kopiera glaze-render.exe
rem        -> paketera .vsix (win32-x64) -> installera i VS Code.
rem  Fragar innan varje sido-effekt (winget-install, VS Code-install).
rem ============================================================

rem Kor alltid i scriptets egen mapp oavsett varifran det startas.
cd /d "%~dp0"

echo ============================================================
echo  GlazeUI VS Code-extension - installation (Windows)
echo ============================================================
echo.
echo Detta script kommer att:
echo   1. Kontrollera att Node.js/npm finns (annars erbjuda winget-install)
echo   2. Kontrollera att glaze-render.exe ar byggd
echo   3. npm install  (extensionens byggberoenden)
echo   4. Kopiera glaze-render.exe  -^>  bin\win32-x64\
echo   5. Paketera en .vsix (target win32-x64)
echo   6. Installera .vsix:en i VS Code (code --install-extension)
echo.
echo Mapp: %CD%
echo.
set /p SVAR="Fortsatta? [J/N] "
if /i not "%SVAR%"=="J" (
    echo Avbrutet.
    goto :end
)
echo.

rem --- 1. Node.js / npm ---------------------------------------
call :ensure_node_path
where node >nul 2>&1
if errorlevel 1 (
    echo Node.js hittades inte i PATH.
    where winget >nul 2>&1
    if errorlevel 1 (
        echo winget saknas ocksa - installera Node.js LTS manuellt fran
        echo https://nodejs.org och kor scriptet igen.
        goto :end
    )
    set /p NSVAR="Installera Node.js LTS via winget nu? [J/N] "
    if /i not "!NSVAR!"=="J" (
        echo Avbrutet - Node.js kravs.
        goto :end
    )
    echo Installerar Node.js LTS ...
    winget install -e --id OpenJS.NodeJS.LTS --accept-source-agreements --accept-package-agreements
    call :ensure_node_path
    where node >nul 2>&1
    if errorlevel 1 (
        echo.
        echo Node.js ar installerat, men PATH uppdateras inte i denna session.
        echo Stang terminalen, oppna en NY, och kor install-ext.bat igen.
        goto :end
    )
)
for /f "delims=" %%V in ('node --version') do echo Node.js %%V hittad.
echo.

rem --- 2. glaze-render.exe ------------------------------------
set "RENDER=..\..\build\apps\glaze-render\glaze-render.exe"
if not exist "%RENDER%" (
    echo glaze-render.exe saknas:  %RENDER%
    echo Bygg forst fran repo-roten:  cmake --build build
    goto :end
)
echo glaze-render.exe finns.
echo.

rem --- 3. npm install ----------------------------------------
echo [1/3] npm install ...
call npm install
if errorlevel 1 ( echo FEL: npm install misslyckades. & goto :end )
echo.

rem --- 4. kopiera binaren ------------------------------------
echo [2/3] kopierar glaze-render.exe -^> bin\win32-x64\ ...
call node scripts\copy-bin.mjs win32-x64
if errorlevel 1 ( echo FEL: copy-bin misslyckades. & goto :end )
echo.

rem --- 5. paketera VSIX --------------------------------------
echo [3/3] paketerar .vsix (win32-x64) ...
call npm run package:win32-x64
if errorlevel 1 ( echo FEL: paketering misslyckades. & goto :end )
echo.

rem Hitta nyaste .vsix i mappen.
set "VSIX="
for /f "delims=" %%F in ('dir /b /o-d *.vsix 2^>nul') do (
    set "VSIX=%%F"
    goto :gotvsix
)
:gotvsix
if not defined VSIX ( echo FEL: ingen .vsix hittades efter paketering. & goto :end )
echo Paketerad: %CD%\%VSIX%
echo.

rem --- 6. installera i VS Code --------------------------------
where code >nul 2>&1
if errorlevel 1 (
    echo VS Code CLI ^(code^) finns inte i PATH.
    echo Installera manuellt i VS Code:
    echo   Extensions-panelen -^> ... -menyn -^> Install from VSIX...
    echo   Valj:  %CD%\%VSIX%
    goto :end
)
set /p ISVAR="Installera %VSIX% i VS Code nu? [J/N] "
if /i not "!ISVAR!"=="J" (
    echo Hoppar over installationen. Filen finns kvar:
    echo   %CD%\%VSIX%
    goto :end
)
call code --install-extension "%VSIX%" --force
if errorlevel 1 ( echo FEL: installationen i VS Code misslyckades. & goto :end )
echo.
echo ============================================================
echo  Klart! Kor "Developer: Reload Window" i VS Code och oppna
echo  en .glazeui-fil sa renderas den som bild.
echo ============================================================

goto :end

rem -----------------------------------------------------------
rem Lagg standard-Node-mappen i PATH om winget nyss installerade
rem den (PATH refreshas inte i en redan igangvarande cmd-session).
:ensure_node_path
where node >nul 2>&1
if not errorlevel 1 goto :eof
if exist "%ProgramFiles%\nodejs\node.exe" set "PATH=%ProgramFiles%\nodejs;%PATH%"
if exist "%ProgramFiles(x86)%\nodejs\node.exe" set "PATH=%ProgramFiles(x86)%\nodejs;%PATH%"
if exist "%LOCALAPPDATA%\Programs\nodejs\node.exe" set "PATH=%LOCALAPPDATA%\Programs\nodejs;%PATH%"
goto :eof

:end
echo.
pause
endlocal
