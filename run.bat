@echo off
title P2P Chat - Multi Client Launcher

echo ==============================
echo  P2P CHAT - ONE STEP EXECUTION
echo ==============================
echo.

REM ---------- ASK USER ----------
set /p CLIENT_COUNT=Enter number of GUI clients to start: 

REM ---------- VALIDATION ----------
if "%CLIENT_COUNT%"=="" (
    echo Invalid input.
    pause
    exit /b
)

echo.
echo Starting server and %CLIENT_COUNT% client(s)...
echo.

REM ---------- START SERVER ----------
echo.
echo Starting Server...
start "Chat Server" cmd /k "cd backend && server.exe"

REM ---------- WAIT FOR SERVER ----------
timeout /t 2 > nul

REM ---------- START CLIENTS ----------
set /a i=1
:START_CLIENTS
if %i% GTR %CLIENT_COUNT% goto DONE

echo Launching Client %i%...
start "Chat Client %i%" cmd /k python frontend\gui_client.py

set /a i+=1
timeout /t 1 > nul
goto START_CLIENTS

:DONE
echo.
echo ==============================
echo  ALL CLIENTS STARTED
echo ==============================
pause