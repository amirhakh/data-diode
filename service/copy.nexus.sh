#!/bin/sh

CONTAINER=nexus
nexusdata="/data/nexus-data/"
nexustemp="/data/nexus-temp/"
outputdir="/data/repo/"

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
    --exclude=$nexusdata'/tmp' \
    --exclude=$nexusdata'/log' \
    --exclude=$nexusdata'/cache' \
    $nexusdata file://$nexustemp
duplicity --no-compress --no-encryption \
    file://$nexustemp $outputdir

}

rsync-diff-tar() {
    # full/incremental tag ?
    idir=$1
    tdir=$2
    odir=$3
    archive=$(tempfile)
    list=${idir}/change-file.list
    outar=${odir}-$(date +"%Y%m%d-%I%M").tar

    [ -d "$tdir" ] || mkdir -p $tdir
    cd ${tdir}
    [ -f "${list}" ] && rm -f ${list}

    rsync -Warv --delete --exclude='tmp' --exclude='log' --exclude='cache'  $idir/* $tdir/ |
    tail -n +2 |
    head -n -3 > ${list}
    
    cp ${idir}/change-file.list ${tdir}/change-file.list
    grep -v -e '^deleting ' -e '/$' ${list} > $archive

    tar -cf ${outar} -T ${archive} change-file.list

    echo ${outar}
    rm $archive
    cd - > /dev/null
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
    test_run=$(rsync -Warvn --delete --exclude='tmp' --exclude='log' --exclude='cache'  $nexusdata/* $nexustemp/)
    blobs=$(echo "$test_run" | grep ^blobs/ -c)
    if [ "$blobs" -gt 0 ]; then
      if [ "$last_blobs" = "$blobs" ]; then
        docker puase $CONTAINER
        rsync-diff-tar $nexusdata $nexustemp $outputdir
        docker unpuase $CONTAINER
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
        docker stop $CONTAINER
        rsync-diff-untar
        docker start $CONTAINER
      else
        last_blobs="$blobs"
        continue
      fi
    fi
  done

fi


