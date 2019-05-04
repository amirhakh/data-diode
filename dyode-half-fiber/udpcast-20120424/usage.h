#define udp_receiver_trivial_usage \
      "[--file file] [--pipe pipe] [--portbase portbase] [--interface net-interface] [--log file] [--ttl time-to-live] [--mcast-all-addr mcast-all-address] [--rcvbuf buf]"
#define udp_receiver_full_usage \
      "Receives a file via UDP multicast\n\n" \
      "Options:\n" \
      "\t--file\tfile where to store received data\n" \
      "\t--pipe\tprogram through which to pipe the data (for example, for uncompressing)\n" \
      "\t--portbase\tUDP ports to use\n" \
      "\t--interface\tnetwork interface to use (eth0, eth1, ...)\n" \
      "\t--log\tlogfile\n" \
      "\t--ttl\tIP \"time to live\". Only needed when attempting to udpcast accross routers\n" \
      "\t--mcast-all-addr\tmulticast address\n" \
      "\t--rcvbuf\tsize of receive buffer\n"

#define udp_sender_trivial_usage \
      "[--file file] [--full-duplex] [--pipe pipe] [--portbase portbase] [--blocksize size] [--interface net-interface] [--mcast-addr data-mcast-address] [--mcast-all-addr mcast-all-address] [--max-bitrate bitrate] [--pointopoint] [--async] [--log file] [--min-slice-size min] [--max-slice-size max] [--slice-size] [--ttl time-to-live] [--print-seed] [--rexmit-hello-interval interval] [--autostart autostart] [--broadcast]"
#define udp_sender_full_usage \
      "Sends a file via UDP multicast\n\n" \
      "\t--file\tfile to be transmitted\n" \
      "\t--full-duplex\toptimize for full duplex network (equipped with a switch, rather than a hub)\n" \
      "\t--pipe\tprogram through which to pipe the data before sending (for instance, a compressor)\n" \
      "\t--portbase\tUDP ports to use\n" \
      "\t--blocksize\tpacket size\n" \
      "\t--interface\tnetwork interface to use (eth0, eth1, ...)\n" \
      "\t--mcast-addr\taddress on which to multicast the data\n" \
      "\t--mcast-all-addr\taddress on which to multicast the control information\n" \
      "\t--max-bitrate\tmaximal bitrate with which to send the data\n" \
      "\t--pointopoint\tpointopoint (unicast) mode, suitable for a single receiver\n" \
      "\t--async\taynchronous mode (do not expect confirmation messages from receivers)\n" \
      "\t--log\tlog file\n" \
      "\t--min-slice-size\tminimal size of a \"slice\"\n" \
      "\t--max-slice-size\tmaximal size of a \"slice\"\n" \
      "\t--slice-size\tinitial slice size\n" \
      "\t--ttl\tIP \"time to live\". Only needed when attempting to udpcast accross routers\n" \
      "\t--print-seed\t\n" \
      "\t--rexmit-hello-interval\thow often to retransmit \"hello\" packets\n" \
      "\t--autostart\tafter how much hello packets to autostart\n" \
      "\t--broadcast\tuse broadcast rather than multicast\n"
