#!/bin/sh

function cleanup {
  echo "exiting"
  umount /mnt/migrate
}
trap cleanup EXIT
trap cleanup INT

echo "mount -t cifs -o rw,username=$user,password=$password //$smbip/$smbpath /mnt/migrate"
mount -t cifs -o rw,username=$user,password=$password //$smbip/$smbpath /mnt/migrate

while true
do
    sleep 5
    #| logger --id migrate -f
    o=$(rsync -Wav --out-format="%t %f %'b" --stats --delete /mnt/migrate/ /migrate/) #| tee -a /var/log/migrate.log
    ol=$(echo "$o" | wc -l)
    test $ol -gt 18 && echo "$o"
done
