#!/bin/sh

while true
do
  docker start -a inputd
  docker start -a outputd
done

