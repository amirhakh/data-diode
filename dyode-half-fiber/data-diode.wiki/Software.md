# WireShark

This is optional, but we found that using [WireShark LINK]() helped us immensely with troubleshooting. 

Somethings that stood out when using WireShark: 
* We improperly configured our ARP initially and we could see those requests.
* We could see the difference between fully connected communication and unidirectional communication
* We could verify one-way communication between machines

# DYODE

I used the code and setup information provided on [https://github.com/wavestone-cdt/dyode](https://github.com/wavestone-cdt/dyode). 

**Most of the following information is taken directly from their github.**

> NOTE: DYODE sends from `inbox` and receives in `outbox`. You could probably change the code to make it more intuitive, we did not.

### Install the following on both machines:
> Files provided on code page of this repository under required packages. This was to prevent connecting highside to the internet.

> It's worth noting that if Highside needs to be updated in the future, proper security protocol would require using a CD-R or DVD-R to move data upstream, then destroy the disc after single use.

* udpcast, which is the tool used to transfer files through the diode
* Python 2

Then, the following modules:

* pymodbus
* pyinotify
* YAML
* asyncore

## Config File
> Config file is provided on code page of this repository

We copied the contents of `dyode-master` into `usr/local/diode` directory. (User preference)

Edit the `config.yaml` which lives in `usr/local/diode`. Do this identically on both machines.

```
config_name: "Data Diode"
config_version: 1.0
config_date: 08-08-2017

dyode_in:
ip: [HIGHSIDE]
mac: [HIGHSIDE]

dyode_out:
ip: [LOWSIDE]
mac: [LOWSIDE]

modules:
"Data Sender"
type: folder
port: 9600
out: [RECEIVING FOLDER PATH]
in: [SENDING FOLDER PATH]

```


# Code Changes Made
> Modified code is provided on code page of this repository

In `dyode.py` on both machines, make the following changes:
* On line 55: We changed the IP address to our highside address 
* On line 55: We changed eth1 to our highside ethernet adapter ID (eth3)
* On line 107: We changed the IP addresses to our lowside IP address
* On line 109: We changed eth1 to our lowside ethernet adapter ID (eth0)

To make compatible with Python 2.6:
* On line 106: Change {:0.0F} to {0:0.0F}

# Forward Error Correction
We were sending massive files so to prevent four hour file transfers we decreased the amount of forward error correction. If you do not have to send gigantic files, and you are using our modified `dyode.py` code, revert code to original.

``` 
//Original Code
//Line 101
command = 'udp-sender --async --fec 8x16/64 --max-bitrate ' \
```

```
// Modified Code
// Line 101
// Sends four times as much data, four times as fast, with half the error checking!
command = 'udp-sender --async --fec 8x8/256 --max-bitrate ' \
```
