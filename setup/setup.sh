#!/usr/bin/env sh

user=diode
data_path=/data
mkdir ${data_path}

useradd -M -d ${data_path}/$user -s /usr/sbin/nologin $user
echo -ne "DataDiode\nDataDiode\n" | passwd $user

mkdir ${data_path}/$user
mkdir ${data_path}/temp
chown $user -R ${data_path}

./setup-smb.sh
# TODO: ./setup-updcast.sh
# TODO: setup ip & interfaces
./setup-dyode.sh
./setup-docker.sh
./setup-nexus-oss.sh
