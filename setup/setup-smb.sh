#!/bin/sh

#sudo apt update
sudo apt install samba

ufw allow 'Samba'

data_path=/data
mkdir ${data_path}
chgrp sambashare ${data_path}


chown diode:sambashare ${data_path}/diode
chmod 2770 ${data_path}/diode

echo -ne "DataDiode\nDataDiode\n" | smbpasswd -s -a diode
smbpasswd -e diode

cp smb.conf /etc/samba/smb.conf

systemctl restart smbd.service nmbd.service
