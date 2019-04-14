#!/bin/sh

function cleanup {
  echo "exiting"
  umount /mnt/backup
}
#trap cleanup EXIT
#trap cleanup INT

if [[ "$type" == "input" ]]; then

  mount -t cifs -o rw,username="$user",password="$password",nosuid,nodev,noexec /"$address" /mnt/backup || exit 1
  try=$(test $try -gt 100 && echo 100 || echo $try )

  while true
  do
    o=$(rsync -Wav --out-format="%t %f %'b" --stats --delete --exclude '@Recycle' /mnt/backup/ /backup/)
    ol=$(echo "$o" | wc -l)
    test $ol -gt 18 && echo "$o" && break
    try=$((try-1))
    test $try -le 0 && break
    sleep 10
  done

  umount /mnt/backup
  exit 0

elif [[ "$type" == "output" ]]; then

  mount -t cifs -o rw,username="$user",password="$password",nosuid,nodev,noexec /"$address" /mnt/backup || exit 1
  o=$(rsync -Wav --stats --delete --exclude '@Recycle' /backup/ /mnt/backup/)
  ol=$(echo "$o" | wc -l)
  test $ol -gt 18 && echo "$o"
  umount /mnt/backup
  exit 0

fi

