IF_UDPRECEIVER(APPLET_ODDNAME(udp-receiver, udp_receiver, BB_DIR_USR_SBIN, BB_SUID_DROP, udp_receiver))
IF_UDPSENDER(APPLET_ODDNAME(udp-sender, udp_sender, BB_DIR_USR_SBIN, BB_SUID_DROP, udp_receiver))
