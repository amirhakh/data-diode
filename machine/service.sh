#!/bin/sh

function cleanup {
  echo "exiting"
  umount /mnt/migrate
}
#trap cleanup EXIT
#trap cleanup INT

if [[ "$type" == "input" ]]; then
  mount -t cifs -o rw,username="$user",password="$password",nosuid,nodev,noexec /"$address" /mnt/migrate || exit 1
  while true
  do
    sleep 5
    o=$(rsync -Wav --out-format="%t %f %'b" --stats --delete --exclude '@Recycle' /mnt/migrate/ /migrate/)
    ol=$(echo "$o" | wc -l)
    test $ol -gt 18 && echo "$o" && break
  done
  exit 0
elif [[ "$type" == "output" ]]; then
  mount -t cifs -o rw,username="$user",password="$password",nosuid,nodev,noexec /"$address" /mnt/migrate || exit 1
  o=$(rsync -Wav --stats --delete --exclude '@Recycle' /migrate/ /mnt/migrate/)
  ol=$(echo "$o" | wc -l)
  test $ol -gt 18 && echo "$o"
  exit 0
fi

