#!/bin/sh

apt-get install vsftpd
apt-get install openssl

# TODO: certificate replacement annually.
openssl req -x509 -nodes -days 365 -newkey rsa:2048 -keyout /etc/ssl/private/vsftpd.pem -out /etc/ssl/private/vsftpd.pem

ufw allow 990/tcp
ufw allow 40000:50000/tcp

cat ./vsftpd.conf > /etc/vsftpd.conf
touch /etc/vsftp.chroot_list
echo salam > /etc/vsftpd.banned_emails

service vsftpd restart
