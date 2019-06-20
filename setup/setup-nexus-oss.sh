#!/usr/bin/env sh

docker pull sonatype/nexus3

# docker volume create --name nexus-data
# docker run ... -v nexus-data:/nexus-data

path=/data/nexus-data
[ -z "$1" ] || path=$1

mkdir ${path} && chown -R 200 ${path}

docker run --restart=always --name nexus -d -p 8081:8081 -v ${path}:/nexus-data sonatype/nexus3
