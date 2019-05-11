#!/usr/bin/env sh

data_path=/data
mkdir ${data_path}

useradd -M -d ${data_path}/diode -s /usr/sbin/nologin -G sambashare diode
echo -ne "DataDiode\nDataDiode\n" | passwd diode

mkdir ${data_path}/diode
mkdir ${data_path}/temp
chown diode -R ${data_path}

./setup-smb.sh
./setup-dyode.sh
