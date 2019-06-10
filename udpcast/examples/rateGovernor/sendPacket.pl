#!/usr/bin/perl
# Written by Alain Knaff

# Small test program to send messages to an UDP port

use strict;

use IO::Socket::INET;
use Getopt::Std;
use English;
use vars qw($opt_p $opt_i $opt_f $opt_h $opt_I);

getopts("p:i:f:hI:");

if(defined $opt_h) {
  die "Usage: $0 -h [-p port] [-i ip] [-f file] [-I mcast_interface]\n";
}

if(!defined $opt_p) {
    die "Port (-p) missing\n";
}

if(!defined $opt_i) {
    $opt_i = '127.0.0.1';
}

my $data;
if(defined $opt_f) {
    open(DATA, "<$opt_f") || die "Could not open $opt_f ($ERRNO)\n";
    sysread(DATA, $data, 64000);
    close(DATA);
} else {
    $data=$ARGV[0];
}

my $sock = 
    new IO::Socket::INET(Proto => 'udp');
if(!defined $sock) {
    die "Could not open socket at port $opt_p ($ERRNO)\n";
}

sub fill_mreq {
  my ($mcast, $if) = @_;

  my $mreq;
  if($if =~ /^([0-9.]*)$/) {
    $mreq = pack("a4C4i", $mcast, split(/\./, $if),0);
  } else {
    my $idx = pack("a16i",$if,0);
    # SIOCGIFINDEX 0x8933
    if(!defined ioctl($sock, 0x8933, $idx)) {
      die "Interface $if not found\n";
    }
    my ($name, $n) = unpack("a16i", $idx);
    $mreq = pack("a4ii", $mcast,0,$n);
  }

  return $mreq;
}



my $iaddr = gethostbyname($opt_i);
my $toAddr = sockaddr_in($opt_p, $iaddr);
if(defined $opt_I) {
  my $mreq=fill_mreq($iaddr, $opt_I);
  # IPPROTO_IP = 0
  # IP_MULTICAST_IF = 32
  $sock->setsockopt(0, 32, $mreq) ||
    die "ERROR: setsockopt: $!\n";
}
my $ret = $sock->send($data, 0, $toAddr);

1;
