#!/bin/sh

idir="/mnt/hard/nexus-data"
odir="/var/lib/docker/volumes/nexus-data/_data"

copy(){
    docker stop nexus
    rsync -WarvP --delete --exclude='tmp' --exclude='log' --exclude='cache'  $idir/* $odir/
    docker start nexus
}

inoty(){
  while inotifywait -r -e modify,create,delete $1
  do
    rsync -War $1/ $2
  done
}

copy

if [[ "$type" == "input" ]]; then

  last_blobs=0
  while true
  do
    sleep 20
    o=$(rsync -WarvPn --delete --exclude='tmp' --exclude='log' --exclude='cache'  $idir/* $odir/)
    blobs=$(echo "$o" | grep ^blobs/ -c)
    if [ "$blobs" -gt 0 ]; then
      if [ "$last_blobs" = "$blobs" ]; then
        copy
      else
        last_blobs="$blobs"
        continue
      fi
    fi
  done
elif [[ "$type" == "output" ]]; then

  last_blobs=0
  while true
  do
    sleep 10
    o=$(rsync -WarvPn --delete --exclude='tmp' --exclude='log' --exclude='cache'  $odir/* $idir/)
    blobs=$(echo "$o" | grep ^blobs/ -c)
    if [ "$blobs" -gt 0 ]; then
      if [ "$last_blobs" = "$blobs" ]; then
        copy
      else
        last_blobs="$blobs"
        continue
      fi
    fi
  done

fi


