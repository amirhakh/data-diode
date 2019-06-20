#!/bin/sh

#sudo apt update
sudo apt install -y samba

ufw allow 'Samba'

user=diode
data_path=/data
mkdir ${data_path}
chgrp sambashare ${data_path}

usermod -a -G sambashare $user
chown $user:sambashare ${data_path}/$user
chmod 2770 ${data_path}/$user

echo -ne "DataDiode\nDataDiode\n" | smbpasswd -s -a $user
smbpasswd -e $user

cp smb.conf /etc/samba/smb.conf

systemctl restart smbd.service nmbd.service
