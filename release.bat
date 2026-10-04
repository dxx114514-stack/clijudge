@echo off
setlocal EnableExtensions
chcp 65001 >nul
cd /d "%~dp0"

rem ============================================================
rem   release.bat - one-click release
rem   usage: release.bat major|minor|patch
rem   does:  npm version (bump package.json only) -> commit "build X.Y.Z"
rem   -> tag vX.Y.Z -> push -> wait CI
rem   commit message "build X.Y.Z" triggers GitHub Release vX.Y.Z;
rem   npm Publish is dispatched explicitly by this script, because a
rem   release created with GITHUB_TOKEN does not trigger other workflows
rem   (GitHub anti-recursion: release: published never fires here)
rem   note: never let npm do the commit - "call" double-expands percents
rem   and turned "-m build %%s" into a literal "build s" commit once
rem ============================================================

set "ARG=%~1"
if "%ARG%"=="" goto :usage
echo %ARG%| findstr /i /r /x /c:"major" /c:"minor" /c:"patch" >nul || goto :usage

where npm >nul 2>nul || (echo [ERROR] npm not found & goto :fail)
where gh  >nul 2>nul || (echo [ERROR] gh not found & goto :fail)
gh auth status >nul 2>&1 || (echo [ERROR] gh not authenticated, run: gh auth login & goto :fail)

set "BRANCH="
for /f "delims=" %%b in ('git rev-parse --abbrev-ref HEAD 2^>nul') do set "BRANCH=%%b"
if /i not "%BRANCH%"=="main" (echo [ERROR] not on main branch ^(current: %BRANCH%^) & goto :fail)
git diff --quiet || (echo [ERROR] tracked files modified, commit or stash first & goto :fail)
git diff --cached --quiet || (echo [ERROR] changes staged, commit or stash first & goto :fail)

for /f "delims=" %%v in ('node -p "require('./package.json').version"') do set "CUR=%%v"
echo.
echo   current : %CUR%
echo   bump    : %ARG%
echo   action  : npm version %ARG% - commit "build ..." - tag v... - push - wait CI
echo   effect  : GitHub Release + npm publish
echo.
choice /c YN /n /m "Proceed? [Y/N] "
if errorlevel 2 (echo cancelled. & exit /b 1)

rem npm only rewrites package.json here; commit + tag are done explicitly
rem below, because "call" double-expands percents: "-m build %%s" turned
rem into a literal "build s" commit once (never let npm create the commit)
call npm version %ARG% --no-git-tag-version
if errorlevel 1 (echo [ERROR] npm version failed & goto :fail)

for /f "delims=" %%v in ('node -p "require('./package.json').version"') do set "NEW=%%v"
git add package.json
git commit -m "build %NEW%"
if errorlevel 1 (echo [ERROR] git commit failed & goto :fail)
git tag -a v%NEW% -m "v%NEW%"
if errorlevel 1 (echo [ERROR] git tag failed & goto :fail)

echo.
echo [OK] version %CUR% -> %NEW%, tag v%NEW% ready to push...

git push origin main --follow-tags
if errorlevel 1 (echo [ERROR] git push failed - fix manually, local commit/tag already created & goto :fail)

for /f "delims=" %%h in ('git rev-parse HEAD') do set "HEADSHA=%%h"

echo [1/3] waiting for "Build and Release" run... (head %HEADSHA:~0,7%)
set /a TRY=0
:poll_build
set /a TRY+=1
if %TRY% GTR 36 (echo [ERROR] timeout waiting for Build and Release to start & goto :fail)
set "RUNSHA="
for /f "delims=" %%i in ('gh run list --workflow "Build and Release" --limit 1 --json headSha --jq ".[0].headSha" 2^>nul') do set "RUNSHA=%%i"
if /i "%RUNSHA%"=="%HEADSHA%" goto :watch_build
timeout /t 5 /nobreak >nul
goto :poll_build

:watch_build
set "RUNID="
for /f "delims=" %%i in ('gh run list --workflow "Build and Release" --limit 1 --json databaseId --jq ".[0].databaseId"') do set "RUNID=%%i"
gh run watch %RUNID% --exit-status
if errorlevel 1 (echo [ERROR] Build and Release FAILED - run: gh run view %RUNID% & goto :fail)
echo [OK] Build and Release succeeded

echo [2/3] dispatching "npm Publish" workflow...
rem release.yml creates the GH Release with GITHUB_TOKEN -> GitHub suppresses
rem that event (workflows triggered by GITHUB_TOKEN never run) -> dispatch it
gh workflow run npm-publish.yml --ref main
if errorlevel 1 (echo [ERROR] failed to dispatch npm Publish & goto :fail)

set /a TRY=0
:poll_npm
set /a TRY+=1
if %TRY% GTR 36 (echo [ERROR] timeout waiting for dispatched npm Publish run & goto :fail)
set "RUNSHA="
for /f "delims=" %%i in ('gh run list --workflow "npm Publish" --event workflow_dispatch --limit 1 --json headSha --jq ".[0].headSha" 2^>nul') do set "RUNSHA=%%i"
if /i "%RUNSHA%"=="%HEADSHA%" goto :watch_npm
timeout /t 5 /nobreak >nul
goto :poll_npm

:watch_npm
set "RUNID="
for /f "delims=" %%i in ('gh run list --workflow "npm Publish" --limit 1 --json databaseId --jq ".[0].databaseId"') do set "RUNID=%%i"
gh run watch %RUNID% --exit-status
if errorlevel 1 (echo [ERROR] npm Publish FAILED - run: gh run view %RUNID% & goto :fail)
echo [OK] npm Publish succeeded

echo.
echo [DONE] released v%NEW%
echo   GitHub : https://github.com/dxxjudges/clijudge/releases/tag/v%NEW%
echo   npm    : https://www.npmjs.com/package/clijudge/v/%NEW%
echo.
pause
exit /b 0

:usage
echo.
echo usage: release.bat major^|minor^|patch
echo.
echo   major   X.0.0   (big)
echo   minor   x.Y.0   (mid)
echo   patch   x.y.Z   (small)
echo.
echo current:
if exist package.json for /f "delims=" %%v in ('node -p "require('./package.json').version" 2^>nul') do echo   %%v
echo.
pause
exit /b 1

:fail
echo.
pause
exit /b 1
