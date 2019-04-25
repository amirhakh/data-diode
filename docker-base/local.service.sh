#!/bin/sh

relase(){
  sleep 30
  docker stop inputd
  docker stop outputd
  exit 0
}

while true
do
  docker start -a inputd
  docker start -a outputd
done

