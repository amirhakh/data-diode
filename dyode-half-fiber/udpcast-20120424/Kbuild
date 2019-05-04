# Makefile for busybox
#
# Copyright (C) 1999-2005 by Erik Andersen <andersen@codepoet.org>
#
# Licensed under the GPL v2, see the file LICENSE in this tarball.

lib-y:=

lib-$(CONFIG_UDPRECEIVER)       += udp-receiver.o
lib-$(CONFIG_UDPRECEIVER)       += socklib.o
lib-$(CONFIG_UDPRECEIVER)       += udpcast.o
lib-$(CONFIG_UDPRECEIVER)       += receiver-diskio.o
lib-$(CONFIG_UDPRECEIVER)       += receivedata.o
lib-$(CONFIG_UDPRECEIVER)       += udpr-negotiate.o
lib-$(CONFIG_UDPRECEIVER)       += produconsum.o
lib-$(CONFIG_UDPRECEIVER)       += fifo.o
lib-$(CONFIG_UDPRECEIVER)       += log.o
lib-$(CONFIG_UDPRECEIVER)       += statistics.o 
lib-$(CONFIG_UDPRECEIVER)       += fec.o
lib-$(CONFIG_UDPRECEIVER)       += udpc_version.o
lib-$(CONFIG_UDPRECEIVER)       += console.o
lib-$(CONFIG_UDPRECEIVER)       += process.o

lib-$(CONFIG_UDPSENDER)         += udp-sender.o
lib-$(CONFIG_UDPSENDER)         += socklib.o
lib-$(CONFIG_UDPSENDER)         += udpcast.o
lib-$(CONFIG_UDPSENDER)         += auto-rate.o
lib-$(CONFIG_UDPSENDER)         += rate-limit.o
lib-$(CONFIG_UDPSENDER)         += rateGovernor.o
lib-$(CONFIG_UDPSENDER)         += sender-diskio.o
lib-$(CONFIG_UDPSENDER)         += senddata.o
lib-$(CONFIG_UDPSENDER)         += udps-negotiate.o
lib-$(CONFIG_UDPSENDER)         += fifo.o
lib-$(CONFIG_UDPSENDER)         += produconsum.o
lib-$(CONFIG_UDPSENDER)         += participants.o
lib-$(CONFIG_UDPSENDER)         += log.o
lib-$(CONFIG_UDPSENDER)         += statistics.o
lib-$(CONFIG_UDPSENDER)         += fec.o
lib-$(CONFIG_UDPSENDER)         += udpc_version.o
lib-$(CONFIG_UDPSENDER)         += console.o
lib-$(CONFIG_UDPSENDER)         += process.o

