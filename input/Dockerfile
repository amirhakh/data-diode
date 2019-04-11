FROM alpine:3.9

RUN apk add --no-cache logrotate dcron cifs-utils rsync

#RUN echo -e "/var/log/migrate.log {\n  rotate 12\n  monthly\n  compress\n  missingok\n  notifempty\n}" > /etc/logrotate.d/migrate
RUN mkdir /mnt/migrate && touch /var/log/cron.log && touch /var/log/migrate.log
RUN crontab -l | { cat; echo -e "0 0 * * 5 rm -rf /mnt/migrate/*\n" ; } | crontab -

COPY service.sh /root/

# ENTRYPOINT [ "bash", "/root/service.sh" ]
CMD crond && /bin/sh /root/service.sh
