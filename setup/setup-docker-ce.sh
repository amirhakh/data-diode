#!/usr/bin/env bash

# curl https://get.docker.com | bash

apt-get remove -y docker docker-engine docker.io containerd runc
apt-get install -y \
    apt-transport-https \
    ca-certificates \
    curl \
    gnupg-agent \
    software-properties-common \
    iptables-persistent \
    inotify-tools

# install Docker
curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo apt-key add -
add-apt-repository \
   "deb [arch=amd64] https://download.docker.com/linux/ubuntu \
   $(lsb_release -cs) \
   stable"
apt-get update
[ -d ./apt ] && dpkg -i ./apt/*.deb || apt-get install -y docker-ce docker-ce-cli containerd.io
[ -f ./apt/docker-compose ] && ./apt/docker-compose cp /usr/local/bin/ || curl -L "https://github.com/docker/compose/releases/download/1.24.0/docker-compose-$(uname -s)-$(uname -m)" -o /usr/local/bin/docker-compose
chmod +x /usr/local/bin/docker-compose
