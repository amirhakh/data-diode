# -*- coding: utf-8 -*-

import datetime
import errno
import hashlib
import logging
import multiprocessing
from multiprocessing import Process, Pipe
import os
import random
import shlex
import shutil
# Imports
import string
import subprocess
import time
import json

# Logging stuff
logging.basicConfig()
global log
log = logging.getLogger()
log.setLevel(logging.DEBUG)


# TODO: one port for sync and some command
# TODO: destination folder empty size check
# TODO: syslog redirect
# TODO: repository (nexus, ubuntu, windows)
# TODO: file access: FTP, SFTP, SMB, web, ...
# TODO: domain access manager
# TODO: get file name from hash
# TODO: append new manifest to previous file list

######################## Reception specific functions ##########################

def parse_manifest(file_path):
    log.error("config file:" + file_path)
    files = []
    dirs = []
    with open(file_path, 'r') as config_file:
        try:
            data = json.load(config_file)

            # for file_path, file_hash in data['files']:
            #     log.debug(file_path + ' :: ' + file_hash())
            #     files[file_path] = file_hash
            files = data['files']
            dirs = data['dirs']
        except:
            pass
    return dirs, files


# File reception forever loop
def file_reception_loop(params):
    # background hash check
    def hash_process(pipe):
        receiver, sender = pipe
        while True:
            f, h = receiver.recv()
            log.debug("check hash for :: %s %s" % (f, h))
            if f is None or h is None:
                break
            elif hash_file(f) != h:
                log.error('Invalid checksum for file ' + f)
                os.remove(f)
            else:
                log.info('File Available: ' + f)
        receiver.close()

    receiver, sender = Pipe()

    hash_p = Process(target=hash_process, args=((receiver, sender),))
    hash_p.daemon = True
    hash_p.start()

    while True:
        wait_for_file(sender, params)
        time.sleep(0.1)

    sender.send((None, None,))
    sender.close()
    hash_p.join()
    if hash_p.is_alive():
        hash_p.terminate()
    del receiver, sender, hash_p


# Launch UDPCast to receive a file
def receive_file(file_path, interface, ip_in, port_base, timeout=0):
    log.debug(port_base)
    # 10.0.1.1
    command = "udp-receiver --nosync --mcast-rdv-addr {0} --interface {1} --portbase {2} -f '{3}'".format(
        ip_in, interface, port_base, file_path)
    if timeout > 0:
        command = command + " --start-timeout {0} --receive-timeout {0}".format(timeout)
    log.debug(command)
    output, err = subprocess.Popen(shlex.split(command), shell=False, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE).communicate()


# File reception function
def wait_for_file(sender, params):
    log.debug('Waiting for file ...')
    log.debug(datetime.datetime.now())
    # YOLO
    log.debug(params)
    # Use a dedicated name for each process to prevent race conditions
    process_name = multiprocessing.current_process().name
    if not os.path.exists(params['temp']):
        os.mkdir(params['temp'])
    manifest_filename = params['temp'] + '/manifest_' + process_name + '.cfg'
    receive_file(manifest_filename, params['interface_out'], params['ip_in'], int(params['port']) + 1)
    log.debug(datetime.datetime.now())
    dirs, files = parse_manifest(manifest_filename)
    if len(files) == 0:
        log.error('No file detected')
        return 0
    log.debug('Manifest content : %s' % files)

    for f, h in files:
        # filename = os.path.basename(f)
        # mkdir on the fly
        file_path = os.path.dirname(f)
        if not os.path.exists(file_path):
            try:
                os.makedirs(file_path)
            except OSError as exc:  # Guard against race condition
                if exc.errno != errno.EEXIST:
                    raise
        temp_file = params['temp'] + '/' + ''.join(
            random.SystemRandom().choice(string.ascii_uppercase + string.digits) for _ in range(12))
        log.debug(datetime.datetime.now())
        receive_file(temp_file, params['interface_out'], params['ip_in'], params['port'])
        log.info('File ' + f + ' received')
        log.debug(datetime.datetime.now())
        shutil.move(temp_file, f)
        sender.send((f, h,))

    os.remove(manifest_filename)
    # todo: background
    for the_file in os.listdir(params['temp']):
        file_path = os.path.join(params['temp'], the_file)
        file_path = os.path.join(params['temp'], the_file)
        shutil.rmtree(file_path)


################### Send specific functions ####################################

# Send a file using udpcast
def send_file(file_path, interface, ip_out, port_base, max_bitrate):
    # 10.0.1.2
    command = 'udp-sender --async --fec 8x8/256 --max-bitrate {:0.0f}m '.format(max_bitrate) \
              + '--mcast-rdv-addr {0} --mcast-data-addr {0} '.format(ip_out) \
              + '--portbase {0} --autostart 1 '.format(port_base) \
              + "--interface {0} -f '{1}'".format(interface, file_path)
    log.debug(command)
    (output, err) = subprocess.Popen(command, stdout=subprocess.PIPE, shell=True).communicate()


# List all files recursively
def list_all_files(root_dir):
    files = []
    dirs = []
    for root, directories, filenames in os.walk(root_dir):
        for directory in directories:
            dirs.append(os.path.join(root, directory))
        for filename in filenames:
            files.append(os.path.join(root, filename))

    return dirs, files


def write_manifest(dirs, files, files_hash, manifest_filename, root, new):
    data = {'dirs': [d.replace(root, new, 1) for d in dirs], 'files': []}
    log.debug("Dirs...")
    log.debug([d.replace(root, new, 1) for d in dirs])
    for f in files:
        data['files'].append([f.replace(root, new, 1), files_hash[f]])
        log.debug(f + ' :: ' + files_hash[f])

    with open(manifest_filename, 'wb') as configfile:
        json.dump(data, configfile)


def file_copy(params):
    # TODO: send existing file (startup service) : not open
    log.debug('Local copy starting ... with params :: %s', params)

    dirs, files = list_all_files(params[1]['in'])
    log.debug('List of files : ' + str(files))
    if len(files) == 0:
        log.debug('No file detected')
        return 0
    manifest_data = {}

    for f in files:
        if os.path.isfile(f):
            manifest_data[f] = hash_file(f)
    log.debug('Writing manifest file')
    # Use a dedicated name for each process to prevent race conditions
    manifest_filename = 'manifest_' + str(params[0]) + '.cfg'
    write_manifest(dirs, files, manifest_data, manifest_filename, params[1]['in'], params[1]["out"])
    log.info('Sending manifest file : ' + manifest_filename)

    log.debug(datetime.datetime.now())
    send_file(manifest_filename,
              params[1]['interface_in'],
              params[1]['ip_out'],
              int(params[1]['port']) + 1,
              params[1]['bitrate'])
    log.debug('Deleting manifest file')
    os.remove(manifest_filename)
    time.sleep(0.1)
    for f in files:
        log.info('Sending ' + f)
        log.debug(datetime.datetime.now())
        send_file(f,
                  params[1]['interface_in'],
                  params[1]['ip_out'],
                  params[1]['port'],
                  params[1]['bitrate'])
        log.info('Deleting ' + f)
        os.remove(f)
    for d in dirs:
        log.info('Deleting ' + d)
        try:
            os.rmdir(d)
        except:
            pass


########################### Shared functions ###################################

def hash_file(file):
    # TODO: use 'sha256sum' command for speedup ?
    BLOCKSIZE = 65536
    hasher = hashlib.sha256()
    with open(file, 'rb') as afile:
        buf = afile.read(BLOCKSIZE)
        while len(buf) > 0:
            hasher.update(buf)
            buf = afile.read(BLOCKSIZE)

    return hasher.hexdigest()
