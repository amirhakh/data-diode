#!/bin/sh

instruction=$1

CONTAINER=nexus
CONTAINER_PORT=8081
nexusdata="/data/$CONTAINER-data/"
nexustemp="/data/$CONTAINER-temp/"
outputdir="/data/repo/"
log_file="$outputdir/$CONTAINER.log"

copy_loop_time=30
extract_loop_time=15

# TODO: weekly delete


rsync_diff_tar() {
  archive=$(tempfile)
  list="${nexusdata}/change-file.list"
  tar_date=$(date +"%Y%m%d-%I%M")
  prev_tar_date=$(tail -1 "$log_file" | cut -d' ' -f1)
  [ "$prev_tar_date" == "" ] && prev_tar_date="0"
  outar="${outputdir}/$CONTAINER""_$prev_tar_date-$tar_date.tar"

  [ -d "$nexustemp" ] || mkdir -p $nexustemp
  cd ${nexustemp}
  [ -f "${list}" ] && rm -f ${list}

  rsync -Warv --delete --exclude='tmp' --exclude='log' "$nexusdata"/* $nexustemp/ |
  tail -n +2 |
  head -n -3 > "${list}"
  
  cp "${nexusdata}/change-file.list" "${nexustemp}/change-file.list"
  grep -v -e '^deleting ' -e '/$' "${list}" > "$archive"

  tar -cf "${outar}" -T "${archive}" change-file.list
  echo "$tar_date" "$outar" >> "$log_file"

  echo "${outar}"
  rm "$archive"
  cd - > /dev/null
}

rsync_diff_untar() {
  ifile=$1
  odir=$2
  [ -d "$nexusdata" ] || mkdir -p $nexusdata
  cd $nexusdata
  list=${nexusdata}/change-file.list
  tar -xf $ifile
  grep -e '^deleting ' $list | while read file; do echo [ -f "$file" ] && rm -f "$file" || rmdir "$file"; done
  cd - > /dev/null
}

check_copy_loop() {
  last_blobs=0
  while true
  do
    sleep $copy_loop_time
    test_run=$(rsync -Warvn --delete --exclude='tmp' --exclude='log' "$nexusdata"/* "$nexustemp"/)
    blobs=$(echo "$test_run" | grep ^blobs/ -c)
    if [ "$blobs" -gt 0 ]; then
      if [ "$last_blobs" = "$blobs" ]; then
        docker stop $CONTAINER
        rsync_diff_tar
        docker start $CONTAINER
        last_blobs=0
      else
        last_blobs="$blobs"
        continue
      fi
    fi
  done
}

check_extract_loop() {
  last_count=0
  last_date=$(cat "$nexusdata/last_date")
  [ "$last_date" == "" ] && last_date=0
  while true
  do
    sleep $extract_loop_time
    file_list=$(ls "$outputdir/$CONTAINER"_* | sort)
    count=$(echo "$file_list" | wc -l)
    if [ "$count" -gt 0 ]; then
      if [ "$last_count" = "$count" ]; then
        docker stop $CONTAINER
        for f in file_list; do
          if $(echo "$f" | grep _"$last_date"-); then
            rsync_diff_untar "$f"
            rm -f $f
            last_date=$(echo "$f" | rev | cut -d- -f1 | rev | cut -d. -f1)
          else
            echo "Error: date sequence not match"
            exit 1
          fi
        done
        docker start $CONTAINER
        last_count=0
        # 1 minute delay
      else
        last_count="$count"
        continue
      fi
    fi
  done
}

case "$instruction" in
  "clean")
    rm -rf "$nexustemp/"* "$outputdir/$CONTAINER"*
  ;;
  "distclean")
    rm -rf "$nexustemp/"* "$outputdir/$CONTAINER"* 
    docker rm $CONTAINER
  ;;
  "setup")
    mkdir -p $nexusdata
    mkdir -p $nexustemp
    docker run -d --name $CONTAINER -p $CONTAINER_PORT:8081 -v "$nexusdata":/nexus-data sonatype/nexus3
  ;;
  "tar")
    rsync_diff_tar
  ;;
  "untar")
    rsync_diff_untar
  ;;
  "input")
    rsync_diff_tar
    check_copy_loop
  ;;
  "output")
    check_extract_loop
  ;;
esac


