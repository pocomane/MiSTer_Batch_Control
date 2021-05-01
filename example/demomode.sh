#!/bin/bash

# This is a very simple example script of a MiSTer "Demo mode".
#
# It starts a random rom every 5 minutes.  During the rom execution, the MiSTer
# menu is disabled and no input are passed to the core.  If you want to quit
# the script and return to the normal MiSTer exection mode, press any gamepad
# button 3 times within 5 seconds. The sceen will flash, but the current core
# and rom will be kept, so you can play the last game.
#
# This script is ment to be run in a ssh connection, not by the Script menu. If
# you want launch it from such menu, a wrapper should be added in the Script
# folder, e.g.:
#   #!/bin/bash
#   /path/to/this/script.sh &; disown

MBC="./mbc_mnt"

start_mister(){
  cd /media/fat
  /media/fat/MiSTer &
  disown
  cd -
  sleep 5
}

stop_mister(){
  sleep 1
  killall MiSTer
}

main(){

  stop_mister

  PREVIOUS_TIME=0
  EVENT_COUNT=0
  CHANGE_ROM="true"
  QUIT="false"
  while true ; do

    if [ "$CHANGE_ROM" = "true" ]; then
      RANDOM_CMD=$($MBC list_content | shuf -n 1)
      RANDOM_SYS=$(echo "$RANDOM_CMD" | sed 's: .*$::')
      RANDOM_ROM=$(echo "$RANDOM_CMD" | sed 's:^[^ ]* ::')
      start_mister
      $MBC load_rom "$RANDOM_SYS" "$RANDOM_ROM"
      stop_mister
    fi

    EVENT_FOUND=$($MBC wait_input 300000)
    CURRENT_TIME=$(date -u +%s)

    if [ "$EVENT_FOUND" = "timeout" ]; then
      CHANGE_ROM="true"
      EVENT_COUNT=1
    else
      # Check 3 keypress in 5 seconds.
      # Note: 3 keypresses + 3 keyreleases = 6 events
      CHANGE_ROM="false"
      if [ $((CURRENT_TIME - PREVIOUS_TIME)) -gt 5 ]; then
        EVENT_COUNT=1
      else
        EVENT_COUNT=$((EVENT_COUNT + 1))
      fi
      PREVIOUS_TIME=$CURRENT_TIME
      if [ $EVENT_COUNT -gt 6 ]; then
        QUIT="true"
      fi
    fi

    if [ "$QUIT" = "true" ]; then
      start_mister
      exit 0
    fi
  done
}

######
main

