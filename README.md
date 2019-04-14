# One direction copy

This project build for safe one side copy data.

## Configuration

You can setup this project on every machine by customizing `docker-compose.yml` file. Network and SMB server must be configured before start project.

1. change **parent** for each network _vlan1_ and _vlan2_. Can check interface by `ip a` command
2. change **subnet** for each network driver
3. change **ipv4_address** for each container
4. change **environment** for each container
    - **address**: smb server *ip* or *server name* with smb folder
    - **user**: smb user
    - **password**: smb password
    - **type**: [input|output]
5. change **device: /mnt/hard/migrate_folder** to path with enough storage (like source & destination)

## Run temporarly

By running `temp.service.sh` run service with temp container and transient interface up link.
You must change config for each interface before start script.

## Install permanent

Run command for install each section:

``` bash
bash host.sh # install requirement and basic setup
docker-compose build # for build docker container
docker-compose -d up # run service
docker-compose logs # view log of service
bash firewall # setup iptabels firewall
bash hardern # harden linux usb, kernel, selinux->enforcing
```

Some useful command:

``` bash
docker-compose [start|stop] # start | stop service
docker-compose [up|down] # setup | clean service
docker-compose ps # show running services
```

## Uninstall

to uninstall can use:

```bash
docker-compose down
docker rm inputd outputd
docker rmi smb_vmo smb_vmi
```

## Todo

- [X] iptable firewall with log
- [X] setup as service
- [ ] compile from source
- [X] schedule service time
- [X] schedule connection time (disconnect every some minute)
- [?] schedule data cleaning
- [X] timeout for inactive service -> poweroff
- [ ] autostart servcie
  - [X] add crontab task to delete file
- [ ] remove unnecessary data
- [X] add log rotate
  - [X] check for docker service output rotate
- [ ] user based structure
- [ ] add user permission manager
- [ ] log per user
- [ ] log for viruses | untrusting | executable | connection | login | ...
- [ ] history for untrusting file
- [ ] migration rule
  - [X] ignore @Recycle folder
  - white file format: `.txt,.pdf,.xml`
  - black file format: `.exe`
  - max file size
  - max transaction time
  - miss rule. but move to folder and alert admin
  - time delay for large or lots of file
- [ ] linux hardening
  - [X] mount with `noexec,nodev,nosuid` option
  - [X] sysctl kernel config
  - [ ] `clamav` antivirus check
  - [ ] remove unused package
  - [ ] remove package manager
  - [ ] mount root as read-only
  - [X] interface down
  - [ ] maximum service run time
- [X] not active interface at same time
- [X] change randomly ip
- [ ] add empty or victim host
- [ ] check running process

## Bugs

- [ ] not test by hacker