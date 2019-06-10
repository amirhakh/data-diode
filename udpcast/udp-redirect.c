/* edited from http://www.brokestream.com/udp_redirect.html

  Build: gcc -o udp-redirect udp-redirect.c

  udp-redirect.c
  Version 2019-05-09

  Copyright (C) 2007 Ivan Tikhonov
  Copyright (C) 2019 Amir Haji Ali Khamseh'i

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

  Ivan Tikhonov, kefeer@brokestream.com
  Amir Haji Ali Khamseh'i, amirhakh@gmail.com

*/

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

void usage(const char * program_path)
{
    printf("Usage: %s recv-ip:port [dest-ip[:port]]\n", program_path);
    printf("Usage: %s recv-ip:port send-ip:port [dest-ip[:port]]\n", program_path);
    printf("Usage: %s -r recv-ip:port [-s send-ip[:send-port]] [-d dest-ip[:dest-port]]\n", program_path);
    printf("Usage: %s recv-ip port  # can seprate ip port\n", program_path);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
    int opt, posind=0;
    char *recv_addr=0, *send_addr=0, *dest_addr=0;
    char *recv_port=0, *send_port=0, *dest_port=0;
    char parse_from_positional = 0;

    while ((opt = getopt(argc, argv, "-r:s:d:")) != -1) {
        char *colon, *dot;
        if(optarg)
        {
            colon = strchr(optarg, ':');
            if(colon)
                *colon = 0;
            dot = strchr(optarg, '.');
        }
        else
            dot = colon = NULL;
        switch (opt) {
        case '\001': // positional args
            posind++;
            if(!recv_port)
                goto parse_rcv;
            else if (!dest_port)
                goto parse_dest;
            else if (!send_port)
                goto parse_send;
            break;
        parse_rcv:
        case 'r':
            if(!recv_addr && !recv_port) {
                if((colon == NULL || colon != optarg) && dot)
                    recv_addr = optarg;
                else
                    recv_addr = "0.0.0.0";
                if(colon)
                    recv_port = colon+1;
            }
            else if(dot)
                goto parse_dest;
            else if(!recv_port)
                recv_port = optarg;
            break;
        parse_dest:
            parse_from_positional += 1;
        case 'd':
            if(!dest_addr && !dest_port) {
                if((colon == NULL || colon != optarg) && dot)
                    dest_addr = optarg;
                if(colon)
                    dest_port = colon+1;
            }
            else if (dot)
                goto parse_send;
            else if(!dest_port)
                dest_port = optarg;
            break;
        parse_send:
            parse_from_positional += 1;
        case 's':
            if(!send_addr && !send_port) {
                if((colon == NULL || colon != optarg) && dot)
                    send_addr = optarg;
                if(colon)
                    send_port = colon+1;
            }
            else if(!send_port)
                send_port = optarg;
            break;
        case '?':
        default:
            usage(argv[0]);
        }
    }

    if(send_port && parse_from_positional == 2)
    {
        char * t = send_addr;
        send_addr = dest_addr;
        dest_addr = t;
        t = send_port;
        send_port = dest_port;
        dest_port = t;
    }

    if (!recv_port || (!dest_addr && !dest_port)) {
        usage(argv[0]);
    }

    if(!recv_addr)
        recv_addr = "0.0.0.0";
    if(!send_addr) // FIXME: find from send ip & subnet
        send_addr = recv_addr;
    if(!send_port)
        send_port = recv_port;
    if(!dest_addr && strcmp(recv_addr, "0.0.0.0") != 0)
        dest_addr = recv_addr;
    if(!dest_port)
        dest_port = recv_port;

    int recv_socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP), send_socket;
    send_socket = recv_socket;
    if(recv_socket < 0) {
		printf("Can't open socket\n");
        exit(EXIT_FAILURE);
	}

	/* At least on Linux the obnoxious IP_MULTICAST_ALL flag is set by default */
    opt = 0;
    setsockopt(recv_socket, IPPROTO_IP, IP_MULTICAST_ALL, &opt, sizeof(opt));

    struct sockaddr_in recv_sockaddr, send_sockaddr, dest_sockaddr, src_sockaddr;
    recv_sockaddr.sin_family = AF_INET;
    recv_sockaddr.sin_addr.s_addr = inet_addr(recv_addr);
    recv_sockaddr.sin_port = htons((uint16_t)atoi(recv_port));
    if(bind(recv_socket, (struct sockaddr *)&recv_sockaddr, sizeof(recv_sockaddr)) == -1) {
        printf("Can't bind our address (%s:%s)\n", recv_addr, recv_port);
        exit(EXIT_FAILURE);
    }
    if (IN_MULTICAST(ntohl(recv_sockaddr.sin_addr.s_addr)))
    {
        struct ip_mreq mreq;
        mreq.imr_multiaddr.s_addr = recv_sockaddr.sin_addr.s_addr;
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        if ( setsockopt(recv_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                        (void *)&mreq,
                        sizeof(mreq)) < 0)
        {
            printf("cannot join multicast group '%s'", inet_ntoa(mreq.imr_multiaddr));
            exit(EXIT_FAILURE);
        }
    }

    if(dest_addr || dest_port) {
        dest_sockaddr.sin_family = AF_INET;
        dest_sockaddr.sin_addr.s_addr = inet_addr(dest_addr);
        dest_sockaddr.sin_port = htons((uint16_t)atoi(dest_port));
    }
    else
        dest_sockaddr = recv_sockaddr;

    if(send_addr || send_port) {
        send_sockaddr.sin_family = AF_INET;
        send_sockaddr.sin_addr.s_addr = inet_addr(send_addr);
        send_sockaddr.sin_port = htons((uint16_t)atoi(send_port));
        if(strcmp(send_addr, recv_addr) != 0 || strcmp(send_port, recv_port) != 0)
        {
            send_socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
            if(send_socket < 0) {
                printf("Can't open socket\n");
                exit(EXIT_FAILURE);
            }
            if(bind(send_socket, (struct sockaddr *)&send_sockaddr, sizeof(send_sockaddr)) == -1) {
                printf("Can't bind our address (%s:%s)\n", send_addr, send_port);
                exit(EXIT_FAILURE);
            }
        }
    }
    else
        send_sockaddr = recv_sockaddr;
    if (IN_MULTICAST(ntohl(dest_sockaddr.sin_addr.s_addr)))
    {
        int ttl = 1;
        if (setsockopt(send_socket, IPPROTO_IP, IP_MULTICAST_TTL, &ttl,
                       sizeof(ttl)) < 0)
        {
            printf("cannot set ttl = %d \n", ttl);
            exit(EXIT_FAILURE);
        }
    }

    while(1) {
        static char buffer[65535];
        unsigned int src_size = sizeof(src_sockaddr);
        ssize_t packet_size = recvfrom(recv_socket, buffer, sizeof(buffer), 0, (struct sockaddr *)&src_sockaddr, &src_size);
#ifdef DEBUG
        printf("Get %ld from (%s:%hu)\n", packet_size, inet_ntoa(src_addr.sin_addr), ntohs(src_addr.sin_port));
#endif
        if(packet_size <= 0)
            continue;
        if(!dest_addr)	{
            sendto(send_socket, buffer, (size_t)packet_size, 0, (struct sockaddr *)&src_sockaddr, src_size);
        } else if(src_sockaddr.sin_addr.s_addr == dest_sockaddr.sin_addr.s_addr &&
                   src_sockaddr.sin_port == dest_sockaddr.sin_port) { // do echo
            if(dest_sockaddr.sin_addr.s_addr) // remove line !?
                sendto(send_socket, buffer, (size_t)packet_size, 0, (struct sockaddr *)&dest_sockaddr, sizeof(dest_sockaddr));
		} else {
            sendto(send_socket, buffer, (size_t)packet_size, 0,(struct sockaddr *)&dest_sockaddr, sizeof(dest_sockaddr));
		}
	}
}
