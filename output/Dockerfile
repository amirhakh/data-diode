FROM alpine:3.9

RUN apk add --no-cache logrotate dcron cifs-utils rsync

#RUN echo -e "/var/log/migrate.log {\n  rotate 12\n  monthly\n  compress\n  missingok\n  notifempty\n}" > /etc/logrotate.d/migrate
RUN mkdir /mnt/migrate && touch /var/log/cron.log && touch /var/log/migrate.log

COPY service.sh /root/
#mount.cifs -o username=migrate,password=12345 //172.17.24.118/datamigrate /tmp/cifs/

# ENTRYPOINT [ "bash", "/root/service.sh" ]
CMD crond && /bin/sh /root/service.sh
