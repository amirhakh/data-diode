#!/bin/sh

source ./env

# install base package
apt-get remove docker docker-engine docker.io containerd runc
apt-get install \
    apt-transport-https \
    ca-certificates \
    curl \
    gnupg-agent \
    software-properties-common \
    iptables-persistent \
    chkrootkit clamav clamav-daemon inotify-tools

# install Docker
curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo apt-key add -
add-apt-repository \
   "deb [arch=amd64] https://download.docker.com/linux/ubuntu \
   $(lsb_release -cs) \
   stable"
apt-get update
[ -d ./apt ] && dpkg -i ./apt/*.deb || apt-get install docker-ce docker-ce-cli containerd.io
[ -f ./apt/docker-compose ] && ./apt/docker-compose cp /usr/local/bin/ || curl -L "https://github.com/docker/compose/releases/download/1.24.0/docker-compose-$(uname -s)-$(uname -m)" -o /usr/local/bin/docker-compose
chmod +x /usr/local/bin/docker-compose

systemctl disable clamav-freshclam.service clamav-daemon.service

docker build ./machine -t machine:backup

mkdir -p "$install_path"
cp -r ./machine ./env ./docker-compose.yml ./backup.service.sh "$install_path"
chmod +x "$install_path"/backup.service.sh

mkdir -p /var/log/backup
echo >> $log_path
[ -f $log_config ] || echo "$log_path {
  su root docker
  daily
  rotate 60
  compress
  delaycompress
  missingok
}" > $log_config
chown root:docker -R /var/log/backup
logrotate $log_config

# crontab -l | grep clamscan && crontab -l | { cat ; echo -e "0 21 * * * clamscan -i -r $local_path" ; } | crontab -
echo -e "0 21 * * * clamscan -i -r $local_path" > /etc/cron.d/backup_scan
service cron reload

cp incremental-backup.service /etc/systemd/system/
chmod 664 /etc/systemd/system/incremental-backup.service

systemctl daemon-reload
systemctl enable incremental-backup.service
systemctl start  incremental-backup.service

freshclam

