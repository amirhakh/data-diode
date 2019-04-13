#!/bin/sh

# install base package
sudo apt-get remove docker docker-engine docker.io containerd runc
sudo apt-get install \
    apt-transport-https \
    ca-certificates \
    curl \
    gnupg-agent \
    software-properties-common \
    iptables-persistent \
    chkrootkit clamav clamav-daemon inotify-tools

# install Docker
curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo apt-key add -
sudo add-apt-repository \
   "deb [arch=amd64] https://download.docker.com/linux/ubuntu \
   $(lsb_release -cs) \
   stable"
sudo apt-get update
sudo apt-get install docker-ce docker-ce-cli containerd.io
sudo curl -L "https://github.com/docker/compose/releases/download/1.24.0/docker-compose-$(uname -s)-$(uname -m)" -o /usr/local/bin/docker-compose
sudo chmod +x /usr/local/bin/docker-compose

docker-compose build
docker-compose up

# start task on 8:00 AM except Friday
crontab -l | { cat ; echo -e "0 8 * * * test $(date +'%a') -ne 5 && docker-compose -f /$(pwd)/docker-compose.yml start" ; } | crontab -
# stop task on 20:00 PM
crontab -l | { cat ; echo -e "0 20 * * * docker-compose -f /$(pwd)/docker-compose.yml stop" ; } | crontab -

freshclam
