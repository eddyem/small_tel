/*
 * This file is part of the SendCoords project.
 * Copyright 2020 Edward V. Emelianov <edward.emelianoff@gmail.com>.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <arpa/inet.h>      // inet_ntop
#include <math.h>           // NAN
#include <netdb.h>          //getaddrinfo
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>     // getaddrinfo, connect, socket
#include <sys/types.h>      // getaddrinfo & socket types
#include <time.h>
#include <usefull_macros.h>

#include "cmdlnopts.h"
#include "stelldaemon.h"

extern glob_pars *GP;

/**
 * convert RA/DEC to string in forman RA: HH:MM:SS.SS, DEC: DD:MM:SS.S
 */
static char *radec2str(double ra, double dec){
    static char buf[1024];
    char sign = '+';
    if(dec < 0){
        sign = '-';
        dec = -dec;
    }

    int h = (int)ra;
    ra -= h; ra *= 60.;
    int m = (int)ra;
    ra -= m; ra *= 60.;

    int d = (int) dec;
    dec -= d; dec *= 60.;
    int dm = (int)dec;
    dec -= dm; dec *= 60.;
    snprintf(buf, 1024, "RA=%02d:%02d:%05.2f, DEC=%c%02d:%02d:%04.1f", h,m,ra, sign,d,dm,dec);
    return buf;
}

/**
 * @brief print_coords - print received coordinates @ terminal
 * @param idat (i) - received data
 */
static void print_coords(indata *dat){
    uint16_t len, type;
    uint32_t ra;
    int32_t dec, status;
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
    len = le16toh(dat->len); type = le16toh(dat->type);
    //tim = le64toh(dat->time);
    ra = le32toh(dat->ra);
    dec = (int32_t)le32toh((uint32_t)dat->dec);
    status = (int32_t)le32toh((uint32_t)dat->status);
#else
    len = dat->len;
    type = dat->type;
    //tim = dat->time;
    ra = dat->ra;
    dec = dat->dec;
    status = dat->status;
#endif
    DBG("len=%d, type=%d, ra=%d, dec=%d, status=%d\n",
        len, type, ra, dec, status);
    if(len != sizeof(indata)){
        WARNX("Field `size` of input data not equal to %d", sizeof(indata));
        return;
    }
    double tagRA = RA2HRS(ra), tagDec = DEC2DEG(dec);
//    DBG("RA: %g, DEC: %g, STATUS: %d", tagRA, tagDec, status);
    printf("%s, STATUS: %d\n", radec2str(tagRA, tagDec), status);
}

static int waittoread(int sock){
    fd_set fds;
    struct timeval timeout;
    int rc;
    timeout.tv_sec = 5; // wait not more than 1 second
    timeout.tv_usec = 0;
    FD_ZERO(&fds);
    FD_SET(sock, &fds);
    rc = select(sock+1, &fds, NULL, NULL, &timeout);
    if(rc < 0){
        WARN("select()");
        return 0;
    }
    if(rc > 0 && FD_ISSET(sock, &fds)) return 1;
    return 0;
}

/**
 * @brief str2ha - convert string with angle/hour HH:MM:SS or DD:MM:SS into double value
 * @param str (i) - input string
 * @param ha (o)  - value
 * @return 0 if all OK
 */
static int str2ha(const char *str, double *ha){
    if(!str || !ha) return 1;
    int hd, m, sign = 1; // hours/degrees, minutes
    float s;  // seconds
    if(3 != sscanf(str, "%d:%d:%f", &hd, &m, &s)){
        return 1;
    }
    if(hd < 0){sign = -1; hd = -hd;}
    *ha = sign * (hd + m/60. + s/3600.);
    return 0;
}

void mk_connection(){
    int sockfd = 0;
    int pointing = FALSE; // ==1 if telescope is pointing
    uint8_t recvBuff[64];
    double ra = FP_NAN, dec = FP_NAN;
    if(GP->ra && GP->dec){
        DBG("Point to %s %s", GP->ra, GP->dec);
        if(str2ha(GP->ra, &ra)){
            WARNX("Wrong RA: %s", GP->ra);
            return;
        }
        if(ra < 0. || ra > 24.){
            WARNX("RA should be in range 0..24h");
            return;
        }
        if(str2ha(GP->dec, &dec)){
            WARNX("Wrong DEC: %s", GP->dec);
            return;
        }
        if(dec < -90. || dec > 90.){
            WARNX("DEC should be in range -90..90degr");
            return;
        }
        pointing = TRUE;
        DBG("RA: %g, DEC: %g", ra, dec);
    }
    struct addrinfo h, *r, *p;
    memset(&h, 0, sizeof(h));
    h.ai_family = AF_INET;
    h.ai_socktype = SOCK_STREAM;
    h.ai_flags = AI_CANONNAME;
    if(!GP) ERRX("Command line arguments not defined!");
    if(getaddrinfo(GP->host, GP->port, &h, &r)){WARN("getaddrinfo()"); return;}
    struct sockaddr_in *ia = (struct sockaddr_in*)r->ai_addr;
#ifdef EBUG
    char str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(ia->sin_addr), str, INET_ADDRSTRLEN);
    printf("canonname: %s, port: %u, addr: %s\n", r->ai_canonname, ntohs(ia->sin_port), str);
#endif
    for(p = r; p; p = p->ai_next){
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            WARN("socket()");
            continue;
        }
        if(connect(sockfd, p->ai_addr, p->ai_addrlen) == -1){
            close(sockfd);
            WARN("connect()");
            continue;
        }
        break; // if we get here, we must have connected successfully
    }
    if(p == NULL){
        // looped off the end of the list with no connection
        WARNX("Failed to connect to socket\n");
        return;
    }
    freeaddrinfo(r);

    if(pointing){
        outdata dat;
        dat.len = htole16(sizeof(outdata));
        dat.type = 0;
        dat.ra = htole32(HRS2RA(ra));
        dat.dec = (int32_t)htole32(DEG2DEC(dec));
        if(send(sockfd, &dat, sizeof(outdata), 0) != sizeof(outdata))
            WARN("send()");
        sleep(1);
    }
    int tstart = time(NULL);
    while(waittoread(sockfd)){
        int n = read(sockfd, recvBuff, sizeof(recvBuff)-1);
        DBG("got %d bytes", n);
        if(n < 1) break;
        if(n == sizeof (indata)){
            indata *idat = (indata*)recvBuff;
            if(!GP->quiet) print_coords(idat);
            if(GP->monitor) continue;
            if(!pointing) break;
            if(idat->status == _10U_STATUS_TRACKING && time(NULL) - tstart > 3) break; // start tracking (+3s pause)
            if(idat->status != _10U_STATUS_SLEWING){
                if(time(NULL) - tstart > 5) signals(idat->status); // exit with error code after 5 seconds waiting
            }else tstart = time(NULL); // reset time if status is slewing
        }
    }
    close(sockfd);
    DBG("End");
    //FREE(msg);
}
