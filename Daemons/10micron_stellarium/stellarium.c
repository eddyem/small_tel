/*
 * This file is part of the mountdaemon_10micron project.
 * Copyright 2026 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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

// base functions for sending/receiving data in stellarium protocol

#include <stdint.h>
#include <usefull_macros.h>
#include <sys/socket.h>
#include <erfam.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "angles.h"
#include "mount.h"
#include "stellarium.h"

#define BUFLEN  256

// running flag
static volatile bool isrunning = false;
static pthread_t mainthread;

//read: 0x14 0x0 0x0 0x0 0x5b 0x5a 0x2e 0xc6 0x8c 0x23 0x5 0x0 0x23 0x9 0xe5 0xaf 0x23 0x2e 0x34 0xed
// command: goto 16h29 24.45 -26d25 55.62
/*
 LITTLE-ENDIAN!!!
 from client:
LENGTH (2 bytes, integer): length of the message
TYPE (2 bytes, integer): 0
TIME (8 bytes, integer): current time on the server computer in microseconds
    since 1970.01.01 UT. Currently unused.
RA (4 bytes, unsigned integer): right ascension of the telescope (J2000)
    a value of 0x100000000 = 0x0 means 24h=0h,
    a value of 0x80000000 means 12h
DEC (4 bytes, signed integer): declination of the telescope (J2000)
    a value of -0x40000000 means -90degrees,
    a value of 0x0 means 0degrees,
    a value of 0x40000000 means 90degrees

to client:
LENGTH (2 bytes, integer): length of the message
TYPE (2 bytes, integer): 0
TIME (8 bytes, integer): current time on the server computer in microseconds
    since 1970.01.01 UT. Currently unused.
RA (4 bytes, unsigned integer): right ascension of the telescope (J2000)
    a value of 0x100000000 = 0x0 means 24h=0h,
    a value of 0x80000000 means 12h
DEC (4 bytes, signed integer): declination of the telescope (J2000)
    a value of -0x40000000 means -90degrees,
    a value of 0x0 means 0degrees,
    a value of 0x40000000 means 90degrees
STATUS (4 bytes, signed integer): status of the telescope, currently unused.
    status=0 means ok, status<0 means some error
*/


#define RAD2DEC(rad)  ((int32_t)((rad) / ERFA_DPI * ((double)0x80000000)))
#define RAD2RA(rad)   ((uint32_t)((rad) / ERFA_DPI * ((double)0x80000000)))
#define DEC2DEG(i32)   (((double)(i32))*90./((double)0x40000000))
#define RA2HRS(u32)    (((double)(u32))*12. /((double)0x80000000))

typedef struct __attribute__((__packed__)){
    uint16_t len;
    uint16_t type;
    uint64_t time;
    uint32_t ra;
    int32_t dec;
} indata;

typedef struct __attribute__((__packed__)){
    uint16_t len;
    uint16_t type;
    uint64_t time;
    uint32_t ra;
    int32_t dec;
    int32_t status;
} outdata;

/**
 * @brief proc_data - process data received from Stellarium
 * @param data - raw data
 * @param len  - its length
 * @return true if all OK
 */
static bool proc_data(uint8_t *data, ssize_t len){
    FNAME();
    if(len != sizeof(indata)){
        WARNX("Bad data size: got %zd instead of %zd!", len, sizeof(indata));
        return false;
    }
    indata *dat = (indata*)data;
    uint16_t L, T;
    uint32_t ra;
    int32_t dec;
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
    L = le16toh(dat->len); T = le16toh(dat->type);
    ra = le32toh(dat->ra);
    dec = (int32_t)le32toh((uint32_t)dat->dec);
#else
    L = dat->len; T = dat->type;
    ra = dat->ra; dec = dat->dec;
#endif
    DBG("got message with len %u & type %u", L, T);
    if(L != len){
        WARNX("Length of message != msg->len");
        return false;
    }
    if(T){
        WARNX("Wrong message type");
        return false;
    }
    // convert RA/DEC to degrees
    double tagRA = RA2HRS(ra), tagDec = DEC2DEG(dec);
    DBG("RA: %u (%g hrs), DEC: %d (%g degr)", ra, tagRA, dec, tagDec);
    LOGMSG("(stellarium) RA: %u (%g hrs), DEC: %d (%g degr)", ra, tagRA, dec, tagDec);
    return (mount_setInpRA(tagRA) && mount_setInpDec(tagDec) && mount_setInpMJD(ERFA_DJM00));
}

/**
 * Send data to user
 * @param data   - data to send
 * @param dlen   - data length
 * @param sockfd - socket fd for sending data
 * @return false if client disconnected
 */
float send_data(uint8_t *data, size_t dlen, int sockfd){
    ssize_t sent = send(sockfd, data, dlen, MSG_NOSIGNAL);
    if(sent != (ssize_t)dlen){
        if(sent == -1 && errno != EINTR){
            WARN("Disconnected?");
            return false;
        }
        WARN("write()");
    }
    return true;
}

/**
 * main socket service procedure
 */
static void *handle_socket(void *sockd){
    FNAME();
    if(!isrunning) return NULL;
    outdata dout;
    int sock = *(int*)sockd;
    dout.len = htole16(sizeof(outdata));
    dout.type = 0;
    while(isrunning){
        // get coordinates
        double Rhrs = 0., Ddeg = 0.;
        if((dout.status = mount_getcoords(&Rhrs, &Ddeg)) == false){
            WARNX("Error: can't get coordinates");
            sleep(1);
            continue;
        }
        double RA = HRS2RAD(Rhrs), Decl = DEG2RAD(Ddeg);
        //DBG("got : %g/%g", RA, Decl);
        dout.ra = htole32(RAD2RA(RA));
        dout.dec = (int32_t)htole32(RAD2DEC(Decl));
        if(!send_data((uint8_t*)&dout, sizeof(outdata), sock)) break;
        if(!sl_canread(sock)){
            sleep(1);
            continue;
        }
        // fill incoming buffer
        uint8_t buff[BUFLEN];
        ssize_t rd = read(sock, buff, BUFLEN-1);
        if(rd <= 0){ // error or disconnect
            DBG("Nothing to read from fd %d (ret: %zd)", sock, rd);
            WARNX("Client disconnected?");
            break;
        }
        buff[rd] = 0;
        DBG("read %zd (%s)", rd, buff);
        if(!proc_data(buff, rd)) dout.status = -1;
        else{
            DBG("sent OK");
            dout.status = 0;
        }
    }
    close(sock);
    return NULL;
}

// Main loop thread: wait connections over socket and create one thread for each
static void* start(void *F){
    if(isrunning){
        WARNX("already running");
        return NULL;
    }
    if(!F){
        WARNX("start(): No arg");
        return NULL;
    }
    int sockfd = *((int*)F);
    if(sockfd < 0){
        WARNX("start(): sockfd=%d", sockfd);
        return NULL;
    }
    DBG("Start main loop, sockfd=%d", sockfd);
    isrunning = true;
    // Main loop
    while(isrunning){
        socklen_t size = sizeof(struct sockaddr_in);
        struct sockaddr_in myaddr;
        int newsock;
        DBG("ACCEPT");
        newsock = accept(sockfd, (struct sockaddr*)&myaddr, &size);
        if(newsock <= 0){
            if(errno == EAGAIN){
                usleep(1000);
                continue; // nothing available
            }
            WARN("accept()");
            LOGWARN("Stellarium socket error in accept()");
            sleep(1);
            continue;
        }
        struct sockaddr_in peer;
        socklen_t peer_len = sizeof(peer);
        if(getpeername(newsock, (struct sockaddr*)&peer, &peer_len) == -1){
            LOGWARN("Stellarium socket error in getpeername()");
            WARN("getpeername()");
            close(newsock);
            continue;
        }
        int sockport = -1;
        if(getsockname(newsock, (struct sockaddr*)&peer, &peer_len) == 0){
            sockport = ntohs(peer.sin_port);
        }
        char *peerIP = inet_ntoa(peer.sin_addr);
        LOGMSG("Got connection from %s @ %d", peerIP, sockport);
        DBG("Peer's IP address is: %s (@port %d)\n", peerIP, sockport);
        /*if(strcmp(peerIP, ACCEPT_IP) && strcmp(peerIP, "127.0.0.1")){
            WARNX("Wrong IP");
            close(newsock);
            continue;
        }*/
        pthread_t rthrd;
        if(pthread_create(&rthrd, NULL, handle_socket, (void*)&newsock)){
            WARN("Can't create socket thread");
            LOGERR("Stellarium socket: error creating listen thread");
        }else{
            DBG("Thread created, detouch");
            pthread_detach(rthrd); // don't care about thread state
        }
    }
    close(sockfd);
    return NULL;
}

bool stellarium_start(int sockfd){
    int fd = sockfd;
    if(pthread_create(&mainthread, NULL, start, (void*)&fd)){
        WARN("pthread_create()");
        return false;
    }
    DBG("Stellarium server started");
    return true;
}

void stellarium_stop(){
    if(!isrunning) return;
    isrunning = false;
    pthread_join(mainthread, NULL);
}
