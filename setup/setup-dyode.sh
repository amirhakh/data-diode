#!/usr/bin/env sh

mkdir /opt/dyode

cd ../dyode-half-fiber/

cp config.yaml dyode.py dyode_in.py dyode_out.py screen.py modbus.py /opt/dyode/
grep dyode_out -A2 config.yaml | tail -2 | sed 's/ //g; s/ip:/OUT_IP=/;s/mac:/OUT_MAC=/' > /opt/dyode/env

chown diode:diode -R /opt/diode

cp dyode-in.service dyode-out.service /lib/systemd/system/
systemctl daemon-reload

cd -
