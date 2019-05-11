# -*- coding: utf-8 -*-
import asyncore
import logging
import multiprocessing
import pyinotify
import shlex
import subprocess
import yaml
from math import floor
import signal

import dyode
import modbus
import screen

# Max bitrate, empirical, should be a bit less than 100 but isn't
MAX_BITRATE = 1000

# Logging
logging.basicConfig()
log = logging.getLogger()
log.setLevel(logging.DEBUG)


class EventHandler(pyinotify.ProcessEvent):
    def process_IN_CLOSE_WRITE(self, event):
        log.info('New file detected :: %s' % event.pathname)
        # If a new file is detected, launch the copy
        dyode.file_copy(multiprocessing.current_process()._args)


# When a new file finished copying in the input folder, send it
def watch_folder(properties):
    log.debug('Function "folder" launched with params %s: ' % properties)

    # inotify kernel watchdog stuff
    wm = pyinotify.WatchManager()
    mask = pyinotify.IN_CLOSE_WRITE
    notifier = pyinotify.AsyncNotifier(wm, EventHandler())
    wdd = wm.add_watch(properties['in'], mask, rec=True)
    log.debug('watching :: %s' % properties['in'])
    asyncore.loop()


def launch_agents(module, properties, bitrate):
    log.debug(module)
    if 'bitrate' not in properties:
        properties['bitrate'] = bitrate
    log.debug(properties)
    if properties['type'] == 'folder':
        log.debug('Instanciating a file transfer module :: %s' % module)
        watch_folder(properties)
    elif properties['type'] == 'modbus':
        log.debug('Modbus agent : %s' % module)
        modbus.modbus_loop(module, properties)
    elif properties['type'] == 'screen':
        log.debug('Screen sharing agent : %s' % module)
        screen.watch_folder(module, properties)


def signal_handler(sig, frame):
    if sig in [signal.SIGINT, signal.SIGTERM]:
        pass


if __name__ == '__main__':
    # signal.signal(signal.SIGINT, signal_handler)
    # signal.signal(signal.SIGTERM, signal_handler)
    with open('config.yaml', 'r') as config_file:
        config = yaml.load(config_file)

    # Log infos about the configuration file
    log.info('Loading config file')
    log.info('Configuration name : %s' % config['config_name'])
    log.info('Configuration version : %s' % config['config_version'])
    log.info('Configuration date : %s' % config['config_date'])
    if 'bitrate' in config:
        bitrate_max = config['bitrate']
        if MAX_BITRATE > bitrate_max > 100:
            MAX_BITRATE = bitrate_max
        elif bitrate_max <= 100:
            MAX_BITRATE = 100

    # Static ARP
    log.info('Dyode input ip : %s (%s) on %s' %
             (config['dyode_in']['ip'], config['dyode_in']['mac'], config['dyode_in']['interface']))
    log.info('Dyode output ip : %s (%s)' % (config['dyode_out']['ip'], config['dyode_out']['mac']))
    # p = subprocess.Popen(shlex.split('arp -s ' + config['dyode_out']['ip'] + ' ' + config['dyode_out']['mac']),
    #                      shell=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    # ip address add 10.0.0.1/28 dev eth1
    output, err = subprocess.Popen(
        shlex.split('arp -s ' + config['dyode_out']['ip'] + ' ' + config['dyode_out']['mac']),
        shell=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE).communicate()

    # Number of modules (needed to calculate bitrate)
    # Only works for file transfer using udpcast
    # TODO : Screen sharing needs more bandwidth
    # TODO : Needs to be updated for socket transfer
    modules_nb = len((config['modules']))
    log.debug('Number of modules : %s' % len(config['modules']))
    bitrate = floor(MAX_BITRATE / modules_nb)
    log.debug('Max bitrate per module : %s mbps' % bitrate)

    # Iterate on modules
    modules = config.get('modules')
    for module, properties in modules.iteritems():
        properties['ip_out'] = config['dyode_out']['ip']
        properties['interface_in'] = config['dyode_in']['interface']
        log.debug('Parsing %s' % module)
        log.debug('Trying to launch a new process for module %s' % module)
        p = multiprocessing.Process(name=str(module), target=launch_agents, args=(module, properties, bitrate))
        p.start()

    # TODO : Check if all modules are still alive and restart the ones that are not
