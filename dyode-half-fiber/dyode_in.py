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
    excl_lst = ['^/.*\\.tmp$']
    excl = pyinotify.ExcludeFilter(excl_lst)
    wm = pyinotify.WatchManager()
    mask = pyinotify.IN_CLOSE_WRITE
    notifier = pyinotify.AsyncNotifier(wm, EventHandler())
    wdd = wm.add_watch(properties['in'], mask, rec=True, auto_add=True, exclude_filter=excl)
    log.debug('watching :: %s' % properties['in'])
    asyncore.loop()


def udp_redirect(properties):
    log.debug('Function "udp-redirect" launched with params %s: ' % properties)

    listen_port = properties['port']
    send_port = properties['port']
    if isinstance(listen_port, dict):
        listen_port = listen_port['src']
        send_port = send_port['int']
    command = 'udp-redirect -r %s:%s -d %s:%s' % (properties['listen_ip'], listen_port,
                                                  properties['ip_out'], send_port,)
    log.debug(command)
    (output, err) = subprocess.Popen(shlex.split(command), shell=False, stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE).communicate()


def launch_agents(module, properties):
    log.debug(module)
    if properties['type'] == 'folder':
        log.debug('Instanciating a file transfer module :: %s' % module)
        watch_folder(properties)
    elif properties['type'] == 'modbus':
        log.debug('Modbus agent : %s' % module)
        modbus.modbus_loop(module, properties)
    elif properties['type'] == 'screen':
        log.debug('Screen sharing agent : %s' % module)
        screen.watch_folder(module, properties)
    elif properties['type'] in ['udp-redirect', 'udp_redirect']:
        log.debug('UDP-redirect agent : %s' % module)
        udp_redirect(properties)


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

    if 'multicast' not in config or 'group' not in config['multicast']:
        output, err = subprocess.Popen(
            shlex.split(
                'arp -s ' + config['dyode_out']['internal']['ip'] + ' ' + config['dyode_out']['internal']['mac']),
            shell=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE).communicate()

    modules_nb = len((config['modules']))
    log.debug('Number of modules : %s' % len(config['modules']))
    bitrate = floor(MAX_BITRATE / modules_nb)
    log.debug('Max bitrate per module : %s mbps' % MAX_BITRATE)

    # Iterate on modules
    modules = config.get('modules')
    modules_nb_bitrate = 0
    for module, properties in modules.iteritems():
        if 'bitrate' in properties:
            MAX_BITRATE = MAX_BITRATE - properties['bitrate']
        else:
            modules_nb_bitrate = modules_nb_bitrate + 1
    if MAX_BITRATE < 0:
        log.error('Sum of bitrate is bigger than the maximum defined !')
        exit(1)
    for module, properties in modules.iteritems():
        if 'multicast' in config and 'group' in config['multicast']:
            properties['ip_multicast'] = True
            properties['ip_out'] = config['multicast']['group']
        else:
            properties['ip_out'] = config['dyode_out']['internal']['ip']
        properties['interface_in'] = config['dyode_in']['internal']['interface']
        if 'bitrate' not in properties:
            properties['bitrate'] = MAX_BITRATE / modules_nb_bitrate
        log.debug('Parsing %s' % module)
        log.debug('Trying to launch a new process for module %s' % module)
        p = multiprocessing.Process(name=str(module), target=launch_agents, args=(module, properties))
        p.start()

    # TODO : Check if all modules are still alive and restart the ones that are not
