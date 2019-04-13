FROM alpine:3.9

RUN apk add --no-cache logrotate dcron cifs-utils rsync
RUN mkdir /mnt/migrate && touch /var/log/cron.log && touch /var/log/migrate.log
# RUN crontab -l | { cat; echo -e "0 0 * * 5 rm -rf /mnt/migrate/*\n" ; } | crontab -

COPY service.sh /root/

WORKDIR /root/
CMD /bin/sh /root/service.sh
# CMD crond && /bin/sh /root/service.sh
