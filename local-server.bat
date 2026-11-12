@echo off
rem A FujiBlast server on this PC with 3 bots in ROOM 1 (blast1): the game
rem starts by itself when a 4th player (you) sits down. Ctrl+C or close the
rem windows to stop. Play it with fblocal.nabu (built with SERVER=localhost:9910).
rem The server and the bots are built from server\ (Go) the first time.
set BIN=%~dp0build\server
if not exist "%BIN%\fujiblastd.exe" (
  pushd "%~dp0server"
  go build -o "%BIN%\\" ./cmd/...
  popd
)
if not exist "%BIN%\fujiblastd.exe" (
  echo Could not build the server: Go is needed ^(go.dev^).
  exit /b 1
)
start "fujiblastd" "%BIN%\fujiblastd.exe" -stream :9910
timeout /t 2 /nobreak >nul
start "blastbot" "%BIN%\blastbot.exe" -table blast1 -n 3 -start 4 -rounds 3
echo Server on port 9910, bots BOT1-BOT3 in ROOM 1. Load fblocal.nabu and pick ROOM 1.
