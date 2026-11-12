#!/bin/sh
# run.sh SCRIPT.lua [NABU]: the NABU PC in MAME, headless, driven by a Lua script,
# talking to a RetroNET adapter on a TCP port (MAME's null_modem to a socket).
#
#   ADAPTER   the adapter to start, run as: $ADAPTER FILE.nabu PORT (it serves the
#             program and RetroNET's TCP calls); unset: one must already be listening
#   PORT      6266
#   MAME      mame (MAME_DIR: the folder to run it in, where its roms are)
#   MBIOS     "-bios reva"
#   THROTTLE  "-nothrottle"; set it to " " for real time (play against a live server)
#
# The scripts (boot.lua: title, name, room list; play.lua: joins ROOM 1 and walks;
# prefill.lua: the adapter's player name; smooth.lua: how the others are drawn moving)
# press keys and the joystick at fixed frames: the program is loaded by about frame 800.
D=$(cd "$(dirname "$0")" && pwd)
N=${2:-$D/../../r2r/nabu/fujiblast.nabu}
PORT=${PORT:-6266}
if [ -n "$ADAPTER" ]; then
    $ADAPTER "$N" $PORT > "$D/adapter.log" 2>&1 &
    APID=$!
    sleep 2
fi
rm -rf "$D/snap"
export FBTEST="$D"
(cd "${MAME_DIR:-.}" && ${MAME:-mame} nabupc ${MBIOS:--bios reva} -kbd nabu_hle -hcca null_modem \
    -bitb socket.127.0.0.1:$PORT -video none -sound none ${THROTTLE:--nothrottle} -skip_gameinfo \
    -snapshot_directory "$D/snap" -script "$D/$1")
[ -n "$APID" ] && kill $APID 2>/dev/null
exit 0
