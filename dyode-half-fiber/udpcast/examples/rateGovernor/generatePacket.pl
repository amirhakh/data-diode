#!/usr/bin/perl

# Small script to generate a packet as would be sent by a Logic Innovations IP
# Encapsulator 3000.
# This packet can then be sent using the sendPacket.pl script included in
# this same directory.
#
# It takes as parameters a number of routes, under the
#    format ip/mask:level/length
#
# Example:
#  ./generatePacket.pl 192.168.1.11/255.255.255.0:100/50000 |
#    ./sendPacket.pl -i 224.1.2.3 -p 5555 -I eth0 -f -

use strict;
use Socket;

my $l = @ARGV;
my $buf = pack("NN", $l, $l*16);

foreach my $i (@ARGV) {
    if($i =~ /(.*)\/(.*):(.*)\/(.*)/) {
	my ($ip, $mask, $level, $length) = ($1,$2,$3,$4);
	$buf .= inet_aton($ip);
	$buf .= inet_aton($mask);
	$buf .= pack("NN", $length, $level);
    } else {
	die "Bad arg $i\n";
    }
}

print $buf;
