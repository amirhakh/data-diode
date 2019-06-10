# -*- coding: utf-8 -*-
import logging
import multiprocessing
import shlex
import subprocess

import yaml

import dyode
import modbus
import screen

# Max bitrate, empirical, should be a bit less than 100 but isn't
MAX_BITRATE = 20

# Logging
logging.basicConfig()
log = logging.getLogger()
log.setLevel(logging.DEBUG)


def udp_redirect(properties):
    log.debug('Function "udp-redirect" launched with params %s: ' % properties)

    listen_port = properties['port']
    send_port = properties['port']
    if isinstance(listen_port, dict):
        listen_port = listen_port['int']
        send_port = send_port['dst']
    command = 'udp-redirect -r %s:%s -d %s:%s' % (properties['ip_in'], listen_port,
                                                  properties['destination_ip'], send_port,)
    log.debug(command)
    (output, err) = subprocess.Popen(shlex.split(command), shell=False, stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE).communicate()


def launch_agents(module, properties):
    if properties['type'] == 'folder':
        log.debug('Instanciating a file transfer module :: %s' % module)
        dyode.file_reception_loop(properties)
    elif properties['type'] == 'modbus':
        log.debug('Modbus agent : %s' % module)
        modbus.modbus_master(module, properties)
    elif properties['type'] == 'screen':
        log.debug('Screen sharing : %s' % module)
        screen_process = multiprocessing.Process(name='http_server', target=screen.http_server,
                                                 args=(module, properties))
        screen_process.start()
    elif properties['type'] in ['udp-redirect', 'udp_redirect']:
        log.debug('UDP-redirect agent : %s' % module)
        udp_redirect(properties)


if __name__ == '__main__':
    with open('config.yaml', 'r') as config_file:
        config = yaml.load(config_file)

    # Log infos about the configuration file
    log.info('Loading config file')
    log.info('Configuration name : ' + config['config_name'])
    log.info('Configuration version : ' + str(config['config_version']))
    log.info('Configuration date : ' + str(config['config_date']))

    # Iterate on modules
    modules = config.get('modules')
    for module, properties in modules.iteritems():
        # print module
        if 'multicast' in config and 'group' in config['multicast']:
            properties['ip_multicast'] = True
            properties['ip_in'] = config['multicast']['group']
        else:
            properties['ip_in'] = config['dyode_in']['internal']['ip']
        properties['interface_out'] = config['dyode_out']['internal']['interface']
        log.debug('Parsing "' + module + '"')
        log.debug('Trying to launch a new process for module "' + str(module) + '"')
        p = multiprocessing.Process(name=str(module), target=launch_agents, args=(module, properties))
        p.start()

    # TODO : Check if all modules are still alive and restart the ones that are not
