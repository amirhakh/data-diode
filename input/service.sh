#!/bin/sh

function cleanup {
  echo "exiting"
  umount /mnt/migrate
}
trap cleanup EXIT
trap cleanup INT

# echo "mount //$smbip/$smbpath /mnt/migrate"
mount -t cifs -o rw,username="$user",password="$password",nosuid,nodev,noexec /"$address" /mnt/migrate

if [[ "$type" == "input" ]]; then
  iput=/mnt/migrate/
  oput=/migrate/
  format="%t %f %'b"
else
  oput=/mnt/migrate/
  iput=/migrate/
  format="%f"
fi

while true
do
    sleep 5
    #| logger --id migrate -f
    o=$(rsync -Wav --out-format="$format" --stats --delete --exclude '@Recycle' "$iput" "$oput")
    ol=$(echo "$o" | wc -l)
    test $ol -gt 18 && echo "$o"
done
