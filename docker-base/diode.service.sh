#!/bin/bash

# source ./env

function cleanup {
  docker stop backup &> /dev/null
  docker network rm vlan 2>&1 > /dev/null
  ip link set "$inlan" down
  ip link set "$unlan" down
}
trap cleanup EXIT
#trap cleanup INT

cleanup

# docker network create -d ipvlan -o ipvlan_mode=l2 ipnet212

while true
do
# in

  docker network create -d macvlan --subnet=192.168.1.0/24 --gateway=192.168.1.1 -o parent="$inlan" vlan 2>&1 >/dev/null
  ip link set "$inlan" up

  docker run --rm -t --privileged --cap-add SYS_ADMIN --env-file "$install_path/envin" \
  --network vlan --ip "192.168.1.$(( RANDOM % 240 + 2 ))" --mac-address "c8:60:00"$(hexdump -n 3 -e '":" /1 "%02X" 2 ""' /dev/random) \
  -v "$local_path":/backup --name backup machine:backup 2>&1 >> $log_path
# test $? -ne 0
  docker network rm vlan 2>&1 > /dev/null
  ip link set "$inlan" down

# out

  docker network create -d macvlan --subnet=192.168.1.0/24 --gateway=192.168.1.1 -o parent="$unlan" vlan 2>&1 > /dev/null
  ip link set "$unlan" up

  docker run --rm -t --privileged --cap-add SYS_ADMIN -env-file "$install_path/envun" \
  --network vlan --ip "192.168.1.$(( RANDOM % 240 + 2 ))" --mac-address "c8:60:00"$(hexdump -n 3 -e '":" /1 "%02X" 2 ""' /dev/random) \
  -v "$local_path":/backup --name backup machine:backup 2>&1 >> $log_path

  docker network rm vlan 2>&1 > /dev/null
  ip link set "$unlan" down

# next task

  current=$(date +%H:%M)
  tom=$(date --date="next day 8 am" +%s)
  now=$(date +%s)
  [[ "$current" > "20:00" || $current < "08:00" ]] && sleep $((tom-now)) || sleep 1
done

