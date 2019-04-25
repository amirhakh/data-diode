Welcome to the data-diode wiki!

My data diode setup sends information from a secure machine (highside), to an unsecure server (lowside). The goal was  to send data files easily to a user accessible server without allowing unsecure communication on highside. This project can be reproduced for low to high communication.

Highside machine: Red Hat 6 (RHEL6). Running python 2.6

Lowside machine: CentOS6

*** 
I heavily based my project on [these instructions](https://www.sans.org/reading-room/whitepapers/firewalls/tactical-data-diodes-industrial-automation-control-systems-36057), but found they were missing some layman information and not suitable for a linux operation (it's based on windows).

I used [Dyode](https://github.com/wavestone-cdt/dyode) software. Any difficulties with Dyode should be directed to their repository.

***
The steps I took to make this work:

1. Create a working bi-directional network using two media converters, fiber cable and ethernet cables. See [Hardware](Hardware) for the shopping list. Test with ping.
2. Set a static IP, and static ARP. This prevents the machines from attempting to handshake. See [Network Configuration](Network-Configuration)
3. Download and configure Dyode. See [Software](Software)
4. Disconnect TX cable from lowside and plug it into the carrier signal box. See [Hardware](Hardware)
