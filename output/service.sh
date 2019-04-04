#!/bin/sh

function cleanup {
  echo "exiting"
  umount /mnt/migrate
}
trap cleanup EXIT
trap cleanup INT

echo "mount //$smbip/$smbpath /mnt/migrate"
mount -t cifs -o rw,username=$user,password=$password,nosuid,nodev,noexec //$smbip/$smbpath /mnt/migrate

while true
do
    sleep 5
    #| logger --id migrate -f
    o=$(rsync -Wav --stats --delete /migrate/ /mnt/migrate/) #| tee -a /var/log/migrate.log
    ol=$(echo "$o" | wc -l)
    test $ol -gt 18 && echo "$o"
done
