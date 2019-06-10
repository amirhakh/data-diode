# DYODE : Do Your Own DyodE
A low-cost (~200 €) data diode aimed at Industrial Control Systems.

## Hardware
We use very standard hardware:
* 3 Copper-Optical converters (TP-link mc210cs tested)
* 2 Raspberry Pis
* A few additional components, such as USB-Ethernet NICs, RJ45 cables, ...


## Software
We use ``udpcast`` to transfer files over a unidirectional channel. Modbsu and screen sharing work over a very simple Python UDP socket implementation.

dependency:
```bash
apt-get install inotify-tools python-pyinotify python-pymodbus python-async python-yaml
```

## Features
At the moment, DYODE can be used for the following usages:
* File transfer
* Modbus data transfer
* Screen sharing

You can take a look at the video to learn more about the usage, and ``INSTALL.md`` for installation guidelines.

## Bugs

* ~~file transfer bigger than 2G~~
* transfer speed over 300 Mb/s UDP packet unordered receive
* `udp-receiver` 1.5s delay on close (thread join and disk write)