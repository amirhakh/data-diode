## Static IP address

Set static IP addresses for each of your machines.

A computer that can only transmit or only receive cannot resolve domain names. It becomes necessary to use IP addresses instead of hostnames, or provide appropriate configuration such as entries in /etc/hosts

[We used this method.](https://www.tecmint.com/set-add-static-ip-address-in-linux/)

## Static ARP

Network routing maps IP addresses to MAC addresses, in a normal network switches send out an Address Resolution Protocol (ARP) to determine this endpoint mapping. If this isn't manually set, the machines will attempt to 'find' each other until they timeout.

Set a permanent static ARP on both machines:

[Use the second method on this page.](http://xmodulo.com/how-to-add-or-remove-static-arp-entry-on-linux.html)

Check the status of your ARP:

`arp -a -n`

and you should see something like:

`? (10.0.0.2) at 00:0c:29:c0:94:bf [ether] PERM on eth0`