/*
 * main.c
 *
 * Copyright 2014 Edward V. Emelianov <eddy@sao.ru, edward.emelianoff@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <arpa/inet.h>
#include <endian.h>
#include <math.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>

#include "emulation.h"
#include "libsofa.h"
#include "main.h"
#include "socket.h"
#include "telescope.h"

// daemon.c
extern void check4running(char *self, char *pidfilename, void (*iffound)(pid_t pid));

// Max amount of connections
#define BACKLOG     (10)
#define BUFLEN      (1024)
// pause for incoming message waiting (out coordinates sent after that timeout)
#define SOCK_TMOUT  (1)

static pid_t childpid = 1; // PID of child process
volatile int global_quit = 0;
// quit by signal
void signals(int sig){
    signal(sig, SIG_IGN);
    if(childpid){ // parent process
        restore_tty(); // restore all parameters
        unlink(GP->pidfile);  // and remove pidfile
        weatherserver_disconnect();
    }    
    DBG("Get signal %d, quit.\n", sig);
    global_quit = 1;
    if(childpid) putlog("PID %d exit with status %d after child's %d death", getpid(), sig, childpid);
    else WARNX("Child %d died with %d", getpid(), sig);
    sleep(1);
    exit(sig);
}

// search a first word after needle without spaces
char* stringscan(char *str, char *needle){
    char *a, *e;
    char *end = str + strlen(str);
    a = strstr(str, needle);
    if(!a) return NULL;
    a += strlen(needle);
    while (a < end && (*a == ' ' || *a == '\r' || *a == '\t')) a++;
    if(a >= end) return NULL;
    e = strchr(a, ' ');
    if(e) *e = 0;
    return a;
}

/**
 * Send data to user
 * @param data   - data to send
 * @param dlen   - data length
 * @param sockfd - socket fd for sending data
 * @return 0 if failed
 */
int send_data(uint8_t *data, size_t dlen, int sockfd){
    size_t sent = write(sockfd, data, dlen);
    if(sent != dlen){
        WARN("write()");
        return 0;
    }
    return 1;
}

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

#define DEG2DEC(degr)  ((int32_t)(degr / 90. * ((double)0x40000000)))
#define HRS2RA(hrs)    ((uint32_t)(hrs / 12. * ((double)0x80000000)))
#define DEC2DEG(i32)   (((double)i32)*90./((double)0x40000000))
#define RA2HRS(u32)    (((double)u32)*12. /((double)0x80000000))

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
 * convert RA/DEC to string in forman RA: HH:MM:SS.SS, DEC: DD:MM:SS.S
 */
char *radec2str(double ra, double dec){
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
    snprintf(buf, 1024, "%d:%d:%.2f %c%d:%d:%.1f", h,m,ra, sign,d,dm,dec);
    return buf;
}

/**
 * send input RA/Decl (j2000!) coordinates to tel
 * ra in hours (0..24), decl in degrees (-90..90)
 * @return 1 if all OK
 */
int setCoords(double ra, double dec){
    char *radec = radec2str(ra, dec);
    DBG("Set RA/Decl to %s", radec);
    putlog("Try to set RA/Decl to %s", radec);
    int (*pointfunction)(double, double) = point_telescope;
    if(GP->emulation) pointfunction = point_emulation;
    return pointfunction(ra, dec);
}

/**
 * @brief proc_data - process data received from Stellarium
 * @param data - raw data
 * @param len  - its length
 * @return 1 if all OK
 */
int proc_data(uint8_t *data, ssize_t len){
    FNAME();
    if(len != sizeof(indata)){
        WARNX("Bad data size: got %zd instead of %zd!", len, sizeof(indata));
        return 0;
    }
    indata *dat = (indata*)data;
    uint16_t L, T;
    //uint64_t tim;
    uint32_t ra;
    int32_t dec;
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
    L = le16toh(dat->len); T = le16toh(dat->type);
    //tim = le64toh(dat->time);
    ra = le32toh(dat->ra);
    dec = (int32_t)le32toh((uint32_t)dat->dec);
#else
    L = dat->len; T = dat->type;
    //tim = dat->time;
    ra = dat->ra; dec = dat->dec;
#endif
    DBG("got message with len %u & type %u", L, T);
    if(L != len){
        WARNX("Length of message != msg->len");
        return 0;
    }
    if(T){
        WARNX("Wrong message type");
        return 0;
    }
    // convert RA/DEC to hours/degrees
    double tagRA = RA2HRS(ra), tagDec = DEC2DEG(dec);
    DBG("RA: %u (%g), DEC: %d (%g)", ra, tagRA, dec, tagDec);
    // check RA/DEC
    horizCrds hnow; // without refraction
    polarCrds p2000, pnow;
    p2000.ra = tagRA/12. * M_PI;
    p2000.dec = tagDec * ERFA_DD2R;
    //             now     J2000  obs    Jnow
    if(get_ObsPlace(NULL, &p2000, NULL, &pnow, &hnow)){
        WARNX("Can't convert coordinates to Jnow");
        return 0;
    }
#ifdef EBUG
    int i[4], j[4]; char pm, pm1;
    eraA2af(2, hnow.az, &pm, i);
    eraA2af(2, hnow.zd, &pm1, j);
    DBG("az: %c%02d %02d %02d.%2.d, zd: %c%02d %02d %02d.%2.d",
        pm, i[0],i[1],i[2],i[3],
        pm1,j[0],j[1],j[2],j[3]);
    eraA2af(2, M_PI_2 - hnow.zd, &pm, i);
    DBG("h: %c%02d %02d %02d.%2.d", pm, i[0],i[1],i[2],i[3]);
#endif
    if(hnow.zd > 80.*ERFA_DD2R){
        WARNX("Z > 80degr, stop telescope");
        putlog("Z>80 - stop!");
        stop_telescope();
        return 0;
    }
    tagRA = (pnow.ra - pnow.eo) / M_PI * 12.;
    tagDec = pnow.dec / ERFA_DD2R;
    if(!setCoords(tagRA, tagDec)) return 0;
    return 1;
}

/**
 * main socket service procedure
 */
void *handle_socket(void *sockd){
    FNAME();
    if(global_quit) return NULL;
    outdata dout;
    int sock = *(int*)sockd;
    dout.len = htole16(sizeof(outdata));
    dout.type = 0;
    int (*getcoords)(double*, double*) = get_telescope_coords;
    if(GP->emulation) getcoords = get_emul_coords;
    while(!global_quit){
        // get coordinates
        double RA = 0., Decl = 0.;
        if((dout.status = getcoords(&RA, &Decl)) < 0){
            WARNX("Error: can't get coordinates");
            sleep(1);
            continue;
        }
        //DBG("got : %g/%g", RA, Decl);
        dout.ra = htole32(HRS2RA(RA));
        dout.dec = (int32_t)htole32(DEG2DEC(Decl));
        if(!send_data((uint8_t*)&dout, sizeof(outdata), sock)) break;
        //DBG("sent ra = %g, dec = %g", RA2HRS(dout.ra), DEC2DEG(dout.dec));
        fd_set readfds;
        struct timeval timeout;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        timeout.tv_sec = SOCK_TMOUT; // wait not more than SOCK_TMOUT second
        timeout.tv_usec = 0;
        int sel = select(sock + 1 , &readfds , NULL , NULL , &timeout);
        if(sel < 0){
            if(errno != EINTR)
                WARN("select()");
            continue;
        }
        if(!(FD_ISSET(sock, &readfds))) continue;
        // fill incoming buffer
        uint8_t buff[BUFLEN+1];
        ssize_t rd = read(sock, buff, BUFLEN);
        buff[rd] = 0;
        DBG("read %zd (%s)", rd, buff);
        if(rd <= 0){ // error or disconnect
            DBG("Nothing to read from fd %d (ret: %zd)", sock, rd);
            break;
        }
        /**************************************
         *       DO SOMETHING WITH DATA       *
         **************************************/
        if(!proc_data(buff, rd)) dout.status = -1;
        else dout.status = 0;
    }
    close(sock);
    return NULL;
}

// thread writing FITS-header file
static void *hdrthread(_U_ void *buf){
    // write FITS-header at most once per second
    while(!global_quit){
        wrhdr();
        usleep(100000); // give a chance to write/read for others
    }
    return NULL;
}

/**
 * @brief opensocket - open socket to port `port`
 * @return socket fd or <0 if failed
 */
static int opensocket(char *port){
    if(!port) return -1;
    int reuseaddr = 1;
    int sock;
    struct addrinfo hints, *res, *p;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    DBG("try to open port %s", port);
    if(getaddrinfo(NULL, port, &hints, &res) != 0){
        WARN("getaddrinfo()");
        return 0;
    }
    /*
    struct sockaddr_in *ia = (struct sockaddr_in*)res->ai_addr;
    char str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(ia->sin_addr), str, INET_ADDRSTRLEN);
    */
    // loop through all the results and bind to the first we can
    for(p = res; p != NULL; p = p->ai_next){
        if((sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1){
            WARN("socket()");
            continue;
        }
        if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(int)) == -1){
            WARN("setsockopt()");
            close(sock);
            continue;
        }
        if(bind(sock, p->ai_addr, p->ai_addrlen) == -1){
            WARN("bind()");
            close(sock);
            continue;
        }
        break; // if we get here, we must have connected successfully
    }
    freeaddrinfo(res);
    // Listen
    if(listen(sock, BACKLOG) == -1){
        WARN("listen");
        putlog("listen() error");
    }
    DBG("listen at %s", port);
    putlog("listen at %s", port);
    return sock;
}

/**
 * @brief waitconn
 * @param sock       - socket fd to accept()
 * @param connthread - thread which to run when connection accepted (it's parameter - socket fd)
 */
static void waitconn(int sock, void *(*connthread)(void*)){
    // Main loop
    while(!global_quit){
        socklen_t size = sizeof(struct sockaddr_in);
        struct sockaddr_in myaddr;
        int newsock;
        newsock = accept(sock, (struct sockaddr*)&myaddr, &size);
        if(newsock <= 0){
            WARN("accept()");
            sleep(1);
            continue;
        }
        struct sockaddr_in peer;
        socklen_t peer_len = sizeof(peer);
        if(getpeername(newsock, (struct sockaddr*)&peer, &peer_len) == -1){
            WARN("getpeername()");
            close(newsock);
            continue;
        }
        int sockport = -1;
        if(getsockname(newsock, (struct sockaddr*)&peer, &peer_len) == 0){
            sockport = ntohs(peer.sin_port);
        }
        char *peerIP = inet_ntoa(peer.sin_addr);
        putlog("Got connection from %s @ %d", peerIP, sockport);
        DBG("Peer's IP address is: %s (@port %d)\n", peerIP, sockport);
        /*if(strcmp(peerIP, ACCEPT_IP) && strcmp(peerIP, "127.0.0.1")){
            WARNX("Wrong IP");
            close(newsock);
            continue;
        }*/
        pthread_t rthrd;
        if(pthread_create(&rthrd, NULL, connthread, (void*)&newsock)){
            putlog("Error creating listen thread");
            ERR(_("Can't create socket thread"));
        }else{
            DBG("Thread created, detouch");
            pthread_detach(rthrd); // don't care about thread state
        }
    }
    close(sock);
}

// thread working with terminal
static void *termthread(_U_ void *buf){
    int sock = opensocket(GP->dbgport);
    if(sock < 0){
        putlog("Can't open debugging socket @ port %s", GP->dbgport);
        ERRX("Can't open debug socket");
    }
    waitconn(sock, term_thread);
    return NULL;
}

static inline void main_proc(){
    pthread_t hthrd, termthrd;
    // connect to telescope
    if(!GP->emulation){
        if(!connect_telescope(GP->device, GP->crdsfile)){
            ERRX(_("Can't connect to telescope device"));
        }
        if(pthread_create(&hthrd, NULL, hdrthread, NULL))
            ERR(_("Can't create writing thread"));
        if(pthread_create(&termthrd, NULL, termthread, NULL))
            ERR(_("Can't create terminal thread"));
    }
    // connect to weather daemon
    if(!weatherserver_connect()){
        DBG("Can't connect to weather server, will try later");
    }
    // open socket
    int sock = opensocket(GP->port);
    if(sock < 0){
        putlog("Can't open socket @ port %s", GP->port);
        ERRX("Can't open stellarium socket");
    }
    waitconn(sock, handle_socket);
    usleep(10000);
    pthread_cancel(hthrd); // cancel reading thread
    pthread_cancel(termthrd);
    pthread_join(hthrd, NULL);
    pthread_join(termthrd, NULL);
}

int main(int argc, char **argv){
    char *self = strdup(argv[0]);
    GP = parse_args(argc, argv);
    initial_setup();

    signal(SIGTERM, signals); // kill (-15) - quit
    signal(SIGKILL, signals); // kill (-9) - quit
    signal(SIGHUP, SIG_IGN);  // hup - ignore
    signal(SIGINT, signals);  // ctrl+C - quit
    signal(SIGQUIT, signals); // ctrl+\ - quit
    signal(SIGTSTP, SIG_IGN); // ignore ctrl+Z

    int fd;
    if((fd = open(GP->crdsfile, O_WRONLY | O_TRUNC | O_CREAT, 0644)) < 0) // test FITS-header file for writing
        ERR(_("Can't open %s for writing"), GP->crdsfile);
    close(fd);

    printf("Daemonize\n");
#ifndef EBUG // daemonize only in release mode
    if(daemon(1, 0)){
        putlog("Err: daemon()");
        ERR("daemon()");
    }
#endif // EBUG
    check4running(self, GP->pidfile, NULL);
    if(GP->logfile) openlogfile(GP->logfile);
    putlog("Starting, master PID=%d", getpid());

#ifndef EBUG
    while(1){
        childpid = fork();
        if(childpid < 0){
            putlog("fork() error");
            ERR("ERROR on fork");
        }
        if(childpid){
            WARNX(_("Created child with PID %d\n"), childpid);
            DBG("Created child with PID %d\n", childpid);
            wait(NULL);
            WARNX(_("Child %d died\n"), childpid);
            DBG("Child %d died\n", childpid);
        }else{
            prctl(PR_SET_PDEATHSIG, SIGTERM); // send SIGTERM to child when parent dies
            main_proc();
            return 0;
        }
    }
#else
    main_proc();
#endif

    return 0;
}
