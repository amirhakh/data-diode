# Data Diode

This project improve [data-diode](https://github.com/wavestone-cdt/dyode) project for better service in fast environment.

## Future

* SMB file share
* 1G connection speed ( limit single file transfer to 300 Mb/s for os UDP packet order)

## Bug

* sync send and receive step delay

## TODO

* migrate to python 3
* one port for sync and some command
* error log file
* destination folder empty size check
* syslog redirect
* repository (nexus, ubuntu, windows)
* file access: FTP, SFTP, web, ...
* domain access manager for users
* append new manifest to previous file list
* add nexus repository (with update core image)
* add **rate-limit** and **drop-rate** to udp-redirect
* add **multicast group** support for udp-redirect
