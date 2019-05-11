#!/bin/sh

inputdir="/mnt/hard/nexus-data"
outputdir="/var/lib/docker/volumes/nexus-data/_data"

copy(){
    docker stop nexus
    rsync -WarvP --delete --exclude='tmp' --exclude='log' --exclude='cache'  $idir/* $odir/
    docker start nexus
}

inoty(){
  while inotifywait -r -e modify,create,delete $1
  do
    rsync -War $1/ $2
  done
}

duplicity-bk(){
duplicity --no-compress --no-encryption  \
    --exclude='/mnt/hard/nexus-data/tmp' \
    --exclude='/mnt/hard/nexus-data/log' \
    --exclude='/mnt/hard/nexus-data/cache' \
    /mnt/hard/nexus-data/ file:///tmp/nxd-dup
duplicity --no-compress --no-encryption \
    file:///tmp/nxd-dup/ /var/lib/docker/volumes/nexus-data/_data/

}

rsync-diff-tar() {
    # tf=$(tempfile)
    # full/incremental tag ?
    idir=$1
    odir=$2
    archive=$(tempfile)
    [ -d "$odir" ] || mkdir -p $odir
    cd ${odir}
    list=${idir}/change-file.list
    [ -f "${list}" ] && rm -f ${list}
    rsync -Warv --delete --exclude='tmp' --exclude='log' --exclude='cache'  $idir/* $odir/ |
    tail -n +2 |
    head -n -3 > ${list}
    cp ${idir}/change-file.list ${odir}/change-file.list
    grep -v -e '^deleting ' -e '/$' ${list} > $archive
    outar=${odir}-$(date +"%Y%m%d-%I%M").tar
    tar -cf ${outar} -T ${archive} change-file.list
    echo ${outar}
    rm $archive
    cd - > /dev/null
# TODO: check for full or incremental
}

rsync-diff-untar() {
    ifile=$1
    odir=$2
    [ -d "$odir" ] || mkdir -p $odir
    cd $odir
    list=${odir}/change-file.list
    tar -xf $ifile
    for i in $(grep -e '^deleting ' $list); do [ -f $i ] && rm -f $i || rmdir $i; done
    cd -
}


rsync-diff-tar

if [[ "$type" == "input" ]]; then

  last_blobs=0
  while true
  do
    sleep 20
    test_run=$(rsync -Warvn --delete --exclude='tmp' --exclude='log' --exclude='cache'  $inputdir/* $outputdir/)
    blobs=$(echo "$test_run" | grep ^blobs/ -c)
    if [ "$blobs" -gt 0 ]; then
      if [ "$last_blobs" = "$blobs" ]; then
        docker puase
        rsync-diff-tar $inputdir $outputdir
      else
        last_blobs="$blobs"
        continue
      fi
    fi
  done
elif [[ "$type" == "output" ]]; then

  last_blobs=0
  while true
  do
    sleep 10

    if [ "$blobs" -gt 0 ]; then
      if [ "$last_blobs" = "$blobs" ]; then
        rsync-diff-untar
      else
        last_blobs="$blobs"
        continue
      fi
    fi
  done

fi


