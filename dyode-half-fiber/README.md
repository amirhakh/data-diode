# DYODE : Do Your Own DyodE

A low-cost (~200 €) data diode aimed at Industrial Control Systems.

## Features

At the moment, DYODE can be used for the following usages:

* File transfer
* Modbus data transfer
* Screen sharing
* UDP packet redirect

## Hardware

We use very standard hardware:

* 2 low cost PC or embedded board (Raspberry Pis) with enough storage and two Ethernet NICs (100M or 1G)
* 3 Copper-Optical converters (100M or 1G. TP-link mc210cs tested)
* A few additional components, such as USB-Ethernet NICs, RJ45 cables, fiber cable, ...

## Software

We use ``udpcast`` to transfer files over a unidirectional channel. Modbsu and screen sharing work over a very simple Python UDP socket implementation.

dependency:

```bash
apt-get install inotify-tools python-pyinotify python-pymodbus python-async python-yaml python-configparser
```

## Configuration

You can config your project to enable or setup feature in `config.yaml` file. A sample config file added in project.

Config file have four section:

1. Info (name, version, date)
2. Global configuraion (bitrate, mutlicast group)
3. diode (dyode) in|out configuration (ip, interface, mac, ...)
4. modules:
    * modbus
    * folder (file transfer)
    * screen (screen sharing transfer)
    * udp-redirect (port forwarding). usage like `syslog`

### Hint's

It is better to set `net.core.rmem_max` kernel parameter to have more IP packet buffer. you can use:

```bash
sysctl -w net.core.rmem_max=26214400 # transient
echo "net.core.rmem_max=26214400" >> /etc/sysctl.conf # permanent
```

You can take a look at the video to learn more about the usage, and ``INSTALL.md`` for installation guidelines.

## Bugs

* ~~file transfer bigger than 2G~~
* transfer speed over 300 Mb/s UDP packet unordered receive
* `udp-receiver` 1.5s delay on close (thread join and disk write)
