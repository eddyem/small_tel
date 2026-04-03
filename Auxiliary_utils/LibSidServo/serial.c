/*
 * This file is part of the libsidservo project.
 * Copyright 2025 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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

#include <asm-generic/termbits.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "kalman.h"
#include "main.h"
#include "movingmodel.h"
#include "serial.h"
#include "ssii.h"

// serial devices FD
static int encfd[2] = {-1, -1}, mntfd = -1;
// main mount data
static mountdata_t mountdata = {0};
// last encoders time and last encoders data - for speed measurement
//static coordval_t lastXenc = {0}, lastYenc = {0};

// mutexes for RW operations with mount device and data
static pthread_mutex_t  mntmutex = PTHREAD_MUTEX_INITIALIZER,
                        datamutex = PTHREAD_MUTEX_INITIALIZER;
// encoders thread and mount thread
static pthread_t encthread, mntthread;
// max timeout for 1.5 bytes of encoder and 2 bytes of mount - for `select`
// this values will be modified later
static struct timeval encRtmout = {.tv_sec = 0, .tv_usec = 100}, // encoder reading timeout
    mnt1Rtmout = {.tv_sec = 0, .tv_usec = 200000}, // first reading
    mntRtmout =  {.tv_sec = 0, .tv_usec = 50000}; // next readings

static volatile int GlobExit = 0;

// encoders raw data
typedef struct __attribute__((packed)){
    uint8_t magick;
    int32_t encY;
    int32_t encX;
    uint8_t CRC[4];
} enc_t;

// calculate current X/Y speeds
void getXspeed(){
    static less_square_t *ls = NULL;
    if(!ls){
        ls = LS_init(Conf.EncoderSpeedInterval / Conf.EncoderReqInterval);
        if(!ls) return;
    }
    double dt = timediff0(&mountdata.encXposition.t);
    double speed = LS_calc_slope(ls, mountdata.encXposition.val, dt);
    if(fabs(speed) < 1.5 * Xlimits.max.speed){
        mountdata.encXspeed.val = speed;
        mountdata.encXspeed.t = mountdata.encXposition.t;
    }
}
void getYspeed(){
    static less_square_t *ls = NULL;
    if(!ls){
        ls = LS_init(Conf.EncoderSpeedInterval / Conf.EncoderReqInterval);
        if(!ls) return;
    }
    double dt = timediff0(&mountdata.encYposition.t);
    double speed = LS_calc_slope(ls, mountdata.encYposition.val, dt);
    if(fabs(speed) < 1.5 * Ylimits.max.speed){
        mountdata.encYspeed.val = speed;
        mountdata.encYspeed.t = mountdata.encYposition.t;
    }
}

/**
 * @brief parse_encbuf - check encoder buffer (for encoder data based on SSII proto) and fill fresh data
 * @param databuf - input buffer with 13 bytes of data
 * @param t - time when databuf[0] got
 */
static void parse_encbuf(uint8_t databuf[ENC_DATALEN], struct timespec *t){
    if(!t) return;
    enc_t *edata = (enc_t*) databuf;
/*
#ifdef EBUG
    DBG("ENCBUF:");
    for(int i = 0; i < ENC_DATALEN; ++i) printf("%02X ", databuf[i]);
    printf("\n");
#endif
*/
    if(edata->magick != ENC_MAGICK){
        DBG("No magick");
        return;
    }
    if(edata->CRC[3]){
        DBG("No 0 @ end: 0x%02x", edata->CRC[3]);
        return;
    }
    uint32_t POS_SUM = 0;
    for(int i = 1; i < 9; ++i) POS_SUM += databuf[i];
    uint8_t x = POS_SUM >> 8;
    if(edata->CRC[0] != x){
        DBG("CRC[0] = 0x%02x, need 0x%02x", edata->CRC[0], x);
        return;
    }
    uint8_t y = ((0xFFFF - POS_SUM) & 0xFF) - x;
    if(edata->CRC[1] != y){
        DBG("CRC[1] = 0x%02x, need 0x%02x", edata->CRC[1], y);
        return;
    }
    y = (0xFFFF - POS_SUM) >> 8;
    if(edata->CRC[2] != y){
        DBG("CRC[2] = 0x%02x, need 0x%02x", edata->CRC[2], y);
        return;
    }
    pthread_mutex_lock(&datamutex);
    mountdata.encXposition.val = Xenc2rad(edata->encX);
    mountdata.encYposition.val = Yenc2rad(edata->encY);
    DBG("Got positions X/Y= %.6g / %.6g", mountdata.encXposition.val, mountdata.encYposition.val);
    mountdata.encXposition.t = *t;
    mountdata.encYposition.t = *t;
    getXspeed(); getYspeed();
    pthread_mutex_unlock(&datamutex);
    //DBG("time = %zd+%zd/1e6, X=%g deg, Y=%g deg", tv->tv_sec, tv->tv_usec, mountdata.encposition.X*180./M_PI, mountdata.encposition.Y*180./M_PI);
}

// try to read 1 byte from encoder; return -1 if nothing to read or -2 if device seems to be disconnected
static int getencbyte(){
    if(encfd[0] < 0) return -1;
    uint8_t byte = 0;
    fd_set rfds;
    do{
        FD_ZERO(&rfds);
        FD_SET(encfd[0], &rfds);
        struct timeval tv = encRtmout;
        int retval = select(encfd[0] + 1, &rfds, NULL, NULL, &tv);
        if(!retval) break;
        if(retval < 0){
            if(errno == EINTR) continue;
            return -1;
        }
        if(FD_ISSET(encfd[0], &rfds)){
            ssize_t l = read(encfd[0], &byte, 1);
            if(l != 1) return -2; // disconnected ??
            break;
        } else return -1;
    }while(1);
    return (int)byte;
}

/**
 * @brief readmntdata - read data
 * @param buffer - input buffer
 * @param maxlen - maximal buffer length
 * @return amount of bytes read or -1 in case of error
 */
static int readmntdata(uint8_t *buffer, int maxlen){
    if(mntfd < 0){
        DBG("mntfd non opened");
        return -1;
    }
    if(!buffer || maxlen < 1) return 0;
    //DBG("ask for %d bytes", maxlen);
    int got = 0;
    fd_set rfds;
    struct timeval tv = mnt1Rtmout;
    do{
        FD_ZERO(&rfds);
        FD_SET(mntfd, &rfds);
        //DBG("select");
        int retval = select(mntfd + 1, &rfds, NULL, NULL, &tv);
        //DBG("returned %d", retval);
        if(retval < 0){
            if(errno == EINTR) continue;
            DBG("Error in select()");
            return -1;
        }
        if(FD_ISSET(mntfd, &rfds)){
            ssize_t l = read(mntfd, buffer, maxlen);
            if(l == 0){
                DBG("read ZERO");
                continue;
            }
            if(l < 0){
                DBG("Mount disconnected?");
                return -2; // disconnected ??
            }
            buffer += l;
            maxlen -= l;
            got += l;
        }else{
            DBG("no new data after %d bytes (%s)", got, buffer - got);
            break;
        }
        tv = mntRtmout;
    }while(maxlen);
    return got;
}

// clear data from input buffer
static void clrmntbuf(){
    if(mntfd < 0) return;
    uint8_t bytes[256];
    fd_set rfds;
    do{
        FD_ZERO(&rfds);
        FD_SET(mntfd, &rfds);
        struct timeval tv = {.tv_sec=0, .tv_usec=10};
        int retval = select(mntfd + 1, &rfds, NULL, NULL, &tv);
        if(retval < 0){
            if(errno == EINTR) continue;
            DBG("Error in select()");
            break;
        }
        if(FD_ISSET(mntfd, &rfds)){
            ssize_t l = read(mntfd, &bytes, 256);
            if(l < 1) break;
            DBG("clr got %zd bytes: %s", l, bytes);
        }else break;
    }while(1);
}

// main encoder thread (for separate encoder): read next data and make parsing
static void *encoderthread1(void _U_ *u){
    if(Conf.SepEncoder != 1) return NULL;
    uint8_t databuf[ENC_DATALEN];
    int wridx = 0, errctr = 0;
    struct timespec tcur;
    while(encfd[0] > -1 && errctr < MAX_ERR_CTR && !GlobExit){
        int b = getencbyte();
        if(b == -2) ++errctr;
        if(b < 0) continue;
        errctr = 0;
//        DBG("Got byte from Encoder: 0x%02X", b);
        if(wridx == 0){
            if((uint8_t)b == ENC_MAGICK){
//                DBG("Got magic -> start filling packet");
                databuf[wridx++] = (uint8_t) b;
            }
            continue;
        }else databuf[wridx++] = (uint8_t) b;
        if(wridx == ENC_DATALEN){
            if(curtime(&tcur)){
                parse_encbuf(databuf, &tcur);
                wridx = 0;
            }
        }
    }
    if(encfd[0] > -1){
        close(encfd[0]);
        encfd[0] = -1;
    }
    return NULL;
}

#define XYBUFSZ     (128)
typedef struct{
    char buf[XYBUFSZ+1];
    int len;
} buf_t;

// write to buffer next data portion; return FALSE in case of error
static int readstrings(buf_t *buf, int fd){
    if(!buf){DBG("Empty buffer"); return FALSE;}
    int L = XYBUFSZ - buf->len;
    if(L < 0){
        DBG("buf not initialized!");
        buf->len = 0;
    }
    if(L == 0){
        DBG("buffer  overfull: %d!", buf->len);
        char *lastn = strrchr(buf->buf, '\n');
        if(lastn){
            fprintf(stderr, "BUFOVR: _%s_", buf->buf);
            ++lastn;
            buf->len = XYBUFSZ - (lastn - buf->buf);
            DBG("Memmove %d", buf->len);
            memmove(lastn, buf->buf, buf->len);
            buf->buf[buf->len] = 0;
        }else buf->len = 0;
        L = XYBUFSZ - buf->len;
    }
    //DBG("read %d bytes from %d", L, fd);
    int got = read(fd, &buf->buf[buf->len], L);
    if(got < 0){
        DBG("read()");
        return FALSE;
    }else if(got == 0){ DBG("NO data"); return TRUE; }
    buf->len += got;
    buf->buf[buf->len] = 0;
    //DBG("buf[%d]: %s", buf->len, buf->buf);
    return TRUE;
}

// return TRUE if got, FALSE if no data found
static int getdata(buf_t *buf, long *out){
    if(!buf || buf->len < 1 || buf->len > (XYBUFSZ+1)){
        return FALSE;
    }
//    DBG("got data");
    // read record between last '\n' and previous (or start of string)
    char *last = &buf->buf[buf->len - 1];
    //DBG("buf: _%s_", buf->buf);
    if(*last != '\n') return FALSE;
    *last = 0;
    //DBG("buf: _%s_", buf->buf);
    char *prev = strrchr(buf->buf, '\n');
    if(!prev) prev = buf->buf;
    else{
        fprintf(stderr, "MORETHANONE: _%s_", buf->buf);
        ++prev; // after last '\n'
    }
    if(out) *out = atol(prev);
    // clear buffer
    buf->len = 0;
    return TRUE;
}

// try to write '\n' asking new data portion; return FALSE if failed
static int asknext(int fd){
    //FNAME();
    if(fd < 0) return FALSE;
    int i = 0;
    for(; i < 5; ++i){
        int l = write(fd, "\n", 1);
        //DBG("l=%d", l);
        if(1 == l) return TRUE;
        usleep(100);
    }
    DBG("5 tries... failed!");
    return FALSE;
}

// main encoder thread for separate encoders as USB devices /dev/encoder_X0 and /dev/encoder_Y0
static void *encoderthread2(void _U_ *u){
    if(Conf.SepEncoder != 2) return NULL;
    DBG("Thread started");
    struct pollfd pfds[2];
    pfds[0].fd = encfd[0]; pfds[0].events = POLLIN;
    pfds[1].fd = encfd[1]; pfds[1].events = POLLIN;
    double t0[2], tstart;
    buf_t strbuf[2] = {0};
    long msrlast[2]; // last encoder data
    double mtlast[2]; // last measurement time
    asknext(encfd[0]); asknext(encfd[1]);
    t0[0] = t0[1] = tstart = timefromstart();
    int errctr = 0;

    // init Kalman for both axes
    Kalman3 kf[2];
    double dt = Conf.EncoderReqInterval; // 1ms encoders step
    double sigma_jx = 1e-6, sigma_jy = 1e-6; // "jerk" sigma
    double xnoice = encoder_noise(X_ENC_STEPSPERREV);
    double ynoice = encoder_noise(Y_ENC_STEPSPERREV);
    kalman3_init(&kf[0], dt, xnoice);
    kalman3_init(&kf[1], dt, ynoice);
    kalman3_set_jerk_noise(&kf[0], sigma_jx);
    kalman3_set_jerk_noise(&kf[1], sigma_jy);

    do{ // main cycle
        if(poll(pfds, 2, 0) < 0){
            DBG("poll()");
            break;
        }
        int got = 0;
        for(int i = 0; i < 2; ++i){
            if(pfds[i].revents && POLLIN){
                if(!readstrings(&strbuf[i], encfd[i])){
                    DBG("ERR");
                    ++errctr;
                    break;
                }
            }
            double curt = timefromstart();
            if(getdata(&strbuf[i], &msrlast[i])) mtlast[i] = curt;
            if(curt - t0[i] >= Conf.EncoderReqInterval){ // get last records
                //DBG("last rec %d, curt=%g, t0=%g, mtlast=%g", i, curt, t0[i], mtlast[i]);
                if(curt - mtlast[i] < 1.5*Conf.EncoderReqInterval){
                    //DBG("time OK");
                    pthread_mutex_lock(&datamutex);
                    double pos = (double)msrlast[i];
                    if(i == 0){
                        pos = Xenc2rad(pos);
                        // Kalman filtering
                        kalman3_predict(&kf[i]);
                        kalman3_update(&kf[i], pos);
                        //DBG("Got pos=%g, kalman: angle=%g, vel=%g, acc=%g",
                        //    pos, kf[i].x[0], kf[i].x[1], kf[i].x[2]);
                        mountdata.encXposition.val = kf[i].x[0];
                        curtime(&mountdata.encXposition.t);
                        /*DBG("msrlast=%ld, Xpos.val=%g, t=%zd; XEzero=%d, SPR=%g",
                            msrlast[i], mountdata.encXposition.val, mountdata.encXposition.t.tv_sec,
                            X_ENC_ZERO, X_ENC_STEPSPERREV);*/
                        getXspeed();
                        //mountdata.encXspeed.val = kf[i].x[1];
                        //mountdata.encXspeed.t = mountdata.encXposition.t;
                    }else{
                        pos = Yenc2rad(pos);
                        kalman3_predict(&kf[i]);
                        kalman3_update(&kf[i], pos);
                        //DBG("Got pos=%g, kalman: angle=%g, vel=%g, acc=%g",
                        //    pos, kf[i].x[0], kf[i].x[1], kf[i].x[2]);
                        mountdata.encYposition.val = kf[i].x[0];
                        curtime(&mountdata.encYposition.t);
                        getYspeed();
                        //mountdata.encYspeed.val = kf[i].x[1];
                        //mountdata.encYspeed.t = mountdata.encYposition.t;
                    }
                    pthread_mutex_unlock(&datamutex);
                }
                if(!asknext(encfd[i])){
                    ++errctr;
                    break;
                }
                t0[i] = (curt - t0[i] < 2.*Conf.EncoderReqInterval) ? t0[i] + Conf.EncoderReqInterval : curt;
                ++got;
            }
        }
        if(got == 2) errctr = 0;
    }while(encfd[0] > -1 && encfd[1] > -1 && errctr < MAX_ERR_CTR && !GlobExit);
    DBG("\n\nEXIT: ERRCTR=%d", errctr);
    for(int i = 0; i < 2; ++i){
        if(encfd[i] > -1){
            close(encfd[i]);
            encfd[i] = -1;
        }
    }
    return NULL;
}

data_t *cmd2dat(const char *cmd){
    if(!cmd) return  NULL;
    data_t *d = calloc(1, sizeof(data_t));
    if(!d) return NULL;
    d->buf = (uint8_t*)strdup(cmd);
    d->len = strlen(cmd);
    d->maxlen = d->len + 1;
    return d;
}
void data_free(data_t **x){
    if(!x || !*x) return;
    free((*x)->buf);
    free(*x);
    *x = NULL;
}

static void chkModStopped(double *prev, double cur, int *nstopped, axis_status_t *stat){
    if(!prev || !nstopped || !stat) return;
    if(isnan(*prev)){
        *stat = AXIS_STOPPED;
        DBG("START");
    }else if(*stat != AXIS_STOPPED){
        if(fabs(*prev - cur) < DBL_EPSILON && ++(*nstopped) > MOTOR_STOPPED_CNT){
            *stat = AXIS_STOPPED;
            DBG("AXIS stopped; prev=%g, cur=%g; nstopped=%d", *prev/M_PI*180., cur/M_PI*180., *nstopped);
        }
    }else if(*prev != cur){
        DBG("AXIS moving");
        *nstopped = 0;
    }
    *prev = cur;
}

// Next two functions runs under locked mountdata_t mutex and shouldn't lock it again!!
static axis_status_t chkstopstat(int32_t *prev, int32_t cur, int32_t tag, int *nstopped, axis_status_t stat){
    if(*prev == INT32_MAX){
        stat = AXIS_STOPPED;
        DBG("START");
    }else if(stat == AXIS_GONNASTOP || (stat != AXIS_STOPPED && cur == tag)){ // got command "stop" or motor is on target
        if(*prev == cur){
            DBG("Test for stop, nstopped=%d", *nstopped);
            if(++(*nstopped) > MOTOR_STOPPED_CNT){
                stat = AXIS_STOPPED;
                DBG("AXIS stopped");
            }
        }else *nstopped = 0;
    }else if(*prev != cur){
        DBG("AXIS moving");
        *nstopped = 0;
    }
    *prev = cur;
    return stat;
}

// check for stopped/pointing states
static void ChkStopped(const SSstat *s, mountdata_t *m){
    static int32_t Xmot_prev = INT32_MAX, Ymot_prev = INT32_MAX; // previous coordinates
    static int Xnstopped = 0, Ynstopped = 0; // counters to get STOPPED state
    axis_status_t Xstat, Ystat;
    Xstat = chkstopstat(&Xmot_prev, s->Xmot, m->Xtarget, &Xnstopped, m->Xstate);
    Ystat = chkstopstat(&Ymot_prev, s->Ymot, m->Ytarget, &Ynstopped, m->Ystate);
    if(Xstat != m->Xstate || Ystat != m->Ystate){
        DBG("Status changed");
        mountdata.Xstate = Xstat;
        mountdata.Ystate = Ystat;
    }
}

// main mount thread
static void *mountthread(void _U_ *u){
    int errctr = 0;
    uint8_t buf[sizeof(SSstat)];
    SSstat *status = (SSstat*) buf;
    bzero(&mountdata, sizeof(mountdata));
    double t0 = timefromstart(), tstart = t0, tcur = t0;
    double oldmt = -100.; // old `millis measurement` time
    static uint32_t oldmillis = 0;
    if(Conf.RunModel){
        double Xprev = NAN, Yprev = NAN; // previous coordinates
        int xcnt = 0, ycnt = 0;
        while(!GlobExit){
            coordpair_t c;
            movestate_t xst, yst;
            // now change data
            getModData(&c, &xst, &yst);
            struct timespec tnow;
            if(!curtime(&tnow) || (tcur = timefromstart()) < 0.) continue;
            pthread_mutex_lock(&datamutex);
            mountdata.encXposition.t = mountdata.encYposition.t = tnow;
            mountdata.encXposition.val = c.X + (drand48() - 0.5)*1e-6; // .2arcsec error
            mountdata.encYposition.val = c.Y + (drand48() - 0.5)*1e-6;
            //DBG("t=%g, X=%g, Y=%g", tnow, c.X.val, c.Y.val);
            if(tcur - oldmt > Conf.MountReqInterval){
                oldmillis = mountdata.millis = (uint32_t)((tcur - tstart) * 1e3);
                mountdata.motYposition.t = mountdata.motXposition.t = tnow;
                if(xst == ST_MOVE)
                    mountdata.motXposition.val = c.X + (c.X - mountdata.motXposition.val)*(drand48() - 0.5)/100.;
                //else
                //    mountdata.motXposition.val = c.X;
                if(yst == ST_MOVE)
                    mountdata.motYposition.val = c.Y + (c.Y - mountdata.motYposition.val)*(drand48() - 0.5)/100.;
                //else
                //    mountdata.motYposition.val = c.Y;
                oldmt = tcur;
            }else mountdata.millis = oldmillis;
            chkModStopped(&Xprev, c.X, &xcnt, &mountdata.Xstate);
            chkModStopped(&Yprev, c.Y, &ycnt, &mountdata.Ystate);
            getXspeed(); getYspeed();
            pthread_mutex_unlock(&datamutex);
            while(timefromstart() - t0 < Conf.EncoderReqInterval) usleep(50);
            t0 = timefromstart();
        }
    }
    // data to get
    data_t d = {.buf = buf, .maxlen = sizeof(buf)};
    // cmd to send
    data_t *cmd_getstat = cmd2dat(CMD_GETSTAT);
    if(!cmd_getstat) goto failed;
    while(mntfd > -1 && errctr < MAX_ERR_CTR && !GlobExit){
        // read data to status
        struct timespec tcur;
        if(!curtime(&tcur)) continue;
        // 80 milliseconds to get answer on GETSTAT
        if(!MountWriteRead(cmd_getstat, &d) || d.len != sizeof(SSstat)){
#ifdef EBUG
            DBG("Can't read SSstat, need %zd got %zd bytes", sizeof(SSstat), d.len);
            for(size_t i = 0; i < d.len; ++i) printf("%02X ", d.buf[i]);
            printf("\n");
#endif
            ++errctr; continue;
        }
        if(SScalcChecksum(buf, sizeof(SSstat)-2) != status->checksum){
            DBG("BAD checksum of SSstat, need %d", status->checksum);
            ++errctr; continue;
        }
        errctr = 0;
        pthread_mutex_lock(&datamutex);
        // now change data
        SSconvstat(status, &mountdata, &tcur);
        ChkStopped(status, &mountdata);
        pthread_mutex_unlock(&datamutex);
        // allow writing & getters
        do{
            usleep(500);
        }while(timefromstart() - t0 < Conf.MountReqInterval);
        t0 = timefromstart();
    }
    data_free(&cmd_getstat);
failed:
    if(mntfd > -1){
        close(mntfd);
        mntfd = -1;
    }
    return NULL;
}

// open device and return its FD or -1
static int ttyopen(const char *path, speed_t speed){
    int fd = -1;
    struct termios2 tty;
    DBG("Try to open %s @ %d", path, speed);
    if((fd = open(path, O_RDWR|O_NOCTTY)) < 0){
        DBG("Can't open device %s: %s", path, strerror(errno));
        return -1;
    }
    if(ioctl(fd, TCGETS2, &tty)){
        DBG("Can't read TTY settings");
        close(fd);
        return -1;
    }
    tty.c_lflag     = 0; // ~(ICANON | ECHO | ECHOE | ISIG)
    tty.c_iflag     = 0; // don't do any changes in input stream
    tty.c_oflag     = 0; // don't do any changes in output stream
    // wihthout "HUPCL" it doesn't disconnects
    tty.c_cflag     = HUPCL | BOTHER | CS8 | CREAD | CLOCAL; // other speed, 8bit, RW, ignore line ctrl
    tty.c_ispeed = speed;
    tty.c_ospeed = speed;
    tty.c_cc[VMIN]  = 0;  // non-canonical mode
    tty.c_cc[VTIME] = 0;
    if(ioctl(fd, TCSETS2, &tty)){
        DBG("Can't set TTY settings");
        close(fd);
        return -1;
    }
    DBG("Check speed: i=%d, o=%d", tty.c_ispeed, tty.c_ospeed);
    if(tty.c_ispeed != (speed_t) speed || tty.c_ospeed != (speed_t)speed){ close(fd); return -1; }
    // try to set exclusive
    if(ioctl(fd, TIOCEXCL)){DBG("Can't make exclusive");}
    return fd;
}

// return FALSE if failed
int openEncoder(){
    // TODO: open real devices in "model" mode too!
    if(Conf.RunModel) return TRUE;
    if(!Conf.SepEncoder) return FALSE; // try to open separate encoder when it's absent
    /*
    encRtmout.tv_sec = 0;
    encRtmout.tv_usec = 100000000 / Conf.EncoderDevSpeed; // 10 bytes
*/
    if(Conf.SepEncoder == 1){ // only one device
        DBG("One device");
        if(encfd[0] > -1) close(encfd[0]);
        encfd[0] = ttyopen(Conf.EncoderDevPath, (speed_t) Conf.EncoderDevSpeed);
        if(encfd[0] < 0) return FALSE;
        if(pthread_create(&encthread, NULL, encoderthread1, NULL)){
            close(encfd[0]);
            encfd[0] = -1;
            return FALSE;
        }
    }else if(Conf.SepEncoder == 2){
        DBG("Two devices!");
        const char* paths[2] = {Conf.EncoderXDevPath, Conf.EncoderYDevPath};
        for(int i = 0; i < 2; ++i){
            if(encfd[i] > -1) close(encfd[i]);
            encfd[i] = ttyopen(paths[i], (speed_t) Conf.EncoderDevSpeed);
            if(encfd[i] < 0) return FALSE;
        }
        if(pthread_create(&encthread, NULL, encoderthread2, NULL)){
            for(int i = 0; i < 2; ++i){
                close(encfd[i]);
                encfd[i] = -1;
            }
            return FALSE;
        }
    }else return FALSE;
    DBG("Encoder opened, thread started");
    return TRUE;
}

// return FALSE if failed
int openMount(){
    // TODO: open real devices in "model" mode too!
    if(Conf.RunModel) goto create_thread;
    if(mntfd > -1) close(mntfd);
    DBG("Open mount %s @ %d", Conf.MountDevPath, Conf.MountDevSpeed);
    mntfd = ttyopen(Conf.MountDevPath, (speed_t) Conf.MountDevSpeed);
    if(mntfd < 0) return FALSE;
    DBG("mntfd=%d", mntfd);
    // clear buffer
    clrmntbuf();
    /*
    mnt1Rtmout.tv_sec = 0;
    mnt1Rtmout.tv_usec = 500000000 / Conf.MountDevSpeed; // 50 bytes * 10bits / speed
    mntRtmout.tv_sec = 0;
    mntRtmout.tv_usec = mnt1Rtmout.tv_usec / 50;
*/
create_thread:
    if(pthread_create(&mntthread, NULL, mountthread, NULL)){
        DBG("Can't create mount thread");
        if(!Conf.RunModel){
            close(mntfd);
            mntfd = -1;
        }
        return FALSE;
    }
    DBG("Mount opened, thread started");
    return TRUE;
}

// close all opened serial devices and quit threads
void closeSerial(){
    GlobExit = 1;
    pthread_mutex_unlock(&datamutex);
    DBG("Give 100ms to proper close");
    usleep(100000);
    DBG("Force closed all devices");
    if(mntfd > -1){
        DBG("Cancel mount thread");
        pthread_cancel(mntthread);
        DBG("join mount thread");
        pthread_join(mntthread, NULL);
        DBG("close mount fd");
        if(mntfd > -1) close(mntfd);
        mntfd = -1;
    }
    if(encfd[0] > -1){
        DBG("Cancel encoder thread");
        pthread_cancel(encthread);
        DBG("join encoder thread");
        pthread_join(encthread, NULL);
        DBG("close encoder's fd");
        if(encfd[0] > -1) close(encfd[0]);
        encfd[0] = -1;
        if(Conf.SepEncoder == 2 && encfd[1] > -1){
            close(encfd[1]);
            encfd[1] = -1;
        }
    }
    GlobExit = 0;
}

// get fresh encoder information
mcc_errcodes_t getMD(mountdata_t  *d){
    if(!d) return MCC_E_BADFORMAT;
    pthread_mutex_lock(&datamutex);
    *d = mountdata;
    pthread_mutex_unlock(&datamutex);
    //DBG("ENCpos: %.10g/%.10g", d->encXposition.val, d->encYposition.val);
    //DBG("millis: %u, encxt: %zd (time: %zd)", d->millis, d->encXposition.t.tv_sec, time(NULL));
    return MCC_E_OK;
}

void setStat(axis_status_t Xstate, axis_status_t Ystate){
    DBG("set x/y state to %d/%d", Xstate, Ystate);
    pthread_mutex_lock(&datamutex);
    mountdata.Xstate = Xstate;
    mountdata.Ystate = Ystate;
    pthread_mutex_unlock(&datamutex);
}

// write-read without locking mutex (to be used inside other functions)
static int wr(const data_t *out, data_t *in, int needeol){
    if((!out && !in) || mntfd < 0){
        DBG("Wrong arguments or no mount fd");
        return FALSE;
    }
    //DBG("clrbuf");
    clrmntbuf();
    if(out){
        //DBG("write %zd bytes (%s)", out->len, out->buf);
        if(out->len != (size_t)write(mntfd, out->buf, out->len)){
            DBG("written bytes not equal to need");
            return FALSE;
        }
        //DBG("eol, mntfd=%d", mntfd);
        if(needeol){
            int g = write(mntfd, "\r", 1); // add EOL
            (void) g;
        }
        //usleep(50000); // add little pause so that the idiot has time to swallow
    }
    if(!in || in->maxlen < 1) return TRUE;
    int got = readmntdata(in->buf, in->maxlen);
    if(got < 0){
        DBG("Error reading mount data!");
        in->len = 0;
        return FALSE;
    }
    in->len = got;
    return TRUE;
}

/**
 * @brief MountWriteRead - write and read @ once (or only read/write)
 * @param out (o) - data to write or NULL if not need
 * @param in  (i) - data to read or NULL if not need
 * @return FALSE if failed
 */
int MountWriteRead(const data_t *out, data_t *in){
    if(Conf.RunModel) return FALSE;
    //double t0 = timefromstart();
    pthread_mutex_lock(&mntmutex);
    int ret = wr(out, in, 1);
    pthread_mutex_unlock(&mntmutex);
    //DBG("Got %gus", (timefromstart()-t0)*1e6);
    return ret;
}
// send binary data - without EOL
int MountWriteReadRaw(const data_t *out, data_t *in){
    if(Conf.RunModel) return FALSE;
    pthread_mutex_lock(&mntmutex);
    int ret = wr(out, in, 0);
    pthread_mutex_unlock(&mntmutex);
    return ret;
}

#if 0
static void logscmd(SSscmd *c){
    printf("Xmot=%d, Ymot=%d, Xspeed=%d, Yspeed=%d\n", c->Xmot, c->Ymot, c->Xspeed, c->Yspeed);
    printf("xychange=0x%02X, Xbits=0x%02X, Ybits=0x%02X\n", c->xychange, c->XBits, c->YBits);
    if(c->checksum != SScalcChecksum((uint8_t*)c, sizeof(SSscmd)-2)) printf("Checksum failed\n");
    else printf("Checksum OK\n");
}
static void loglcmd(SSlcmd *c){
    printf("Xmot=%d, Ymot=%d, Xspeed=%d, Yspeed=%d\n", c->Xmot, c->Ymot, c->Xspeed, c->Yspeed);
    printf("Xadder=%d, Yadder=%d, Xatime=%d, Yatime=%d\n", c->Xadder, c->Yadder, c->Xatime, c->Yatime);
    if(c->checksum != SScalcChecksum((uint8_t*)c, sizeof(SSlcmd)-2)) printf("Checksum failed\n");
    else printf("Checksum OK\n");
}
#endif

// send short/long binary command; return FALSE if failed
static int bincmd(uint8_t *cmd, int len){
    if(Conf.RunModel) return FALSE;
    static data_t *dscmd = NULL, *dlcmd = NULL;
    if(!dscmd) dscmd = cmd2dat(CMD_SHORTCMD);
    if(!dlcmd) dlcmd = cmd2dat(CMD_LONGCMD);
    int ret = FALSE;
    pthread_mutex_lock(&mntmutex);
    if(len == sizeof(SSscmd)){
        ((SSscmd*)cmd)->checksum = SScalcChecksum(cmd, len-2);
        //DBG("Short command");
#if 0
        logscmd((SSscmd*)cmd);
#endif
        if(!wr(dscmd, NULL, 1)) goto rtn;
    }else if(len == sizeof(SSlcmd)){
        ((SSlcmd*)cmd)->checksum = SScalcChecksum(cmd, len-2);
       // DBG("Long command");
#if 0
        loglcmd((SSlcmd*)cmd);
#endif
        if(!wr(dlcmd, NULL, 1)) goto rtn;
    }else{
        goto rtn;
    }
    SSstat ans;
    data_t d, in;
    d.buf = cmd;
    d.len = d.maxlen = len;
    in.buf = (uint8_t*)&ans; in.maxlen = sizeof(SSstat);
    ret = wr(&d, &in, 0);
    DBG("%s", ret ? "SUCCESS" : "FAIL");
    if(ret){
        SSscmd *sc = (SSscmd*)cmd;
        mountdata.Xtarget = sc->Xmot;
        mountdata.Ytarget = sc->Ymot;
        DBG("ANS: Xmot/Ymot: %d/%d, Ylast/Ylast: %d/%d; Xtag/Ytag: %d/%d",
            ans.Xmot, ans.Ymot, ans.XLast, ans.YLast, mountdata.Xtarget, mountdata.Ytarget);
    }
rtn:
    pthread_mutex_unlock(&mntmutex);
    return ret;
}

// short, long and config text-binary commands
// return TRUE if OK
int cmdS(SSscmd *cmd){
    return bincmd((uint8_t *)cmd, sizeof(SSscmd));
}
int cmdL(SSlcmd *cmd){
    return bincmd((uint8_t *)cmd, sizeof(SSlcmd));
}
// rw == 1 to write, 0 to read
int cmdC(SSconfig *conf, int rw){
    if(Conf.RunModel) return FALSE;
    static data_t *wcmd = NULL, *rcmd = NULL;
    int ret = FALSE;
    // dummy buffer to clear trash in input
    char ans[300];
    data_t a = {.buf = (uint8_t*)ans, .maxlen=299};
    if(!wcmd) wcmd = cmd2dat(CMD_PROGFLASH);
    if(!rcmd) rcmd = cmd2dat(CMD_DUMPFLASH);
    pthread_mutex_lock(&mntmutex);
    if(rw){ // write
        if(!wr(wcmd, &a, 1)) goto rtn;
    }else{ // read
        data_t d;
        d.buf = (uint8_t *) conf;
        d.len = 0; d.maxlen = 0;
        ret = wr(rcmd, &d, 1);
        DBG("write command: %s", ret ? "TRUE" : "FALSE");
        if(!ret) goto rtn;
        // make a huge pause for stupid SSII
        usleep(100000);
        d.len = 0;  d.maxlen = sizeof(SSconfig);
        ret = wr(rcmd, &d, 1);
        DBG("wr returned %s; got %zd bytes of %zd", ret ? "TRUE" : "FALSE", d.len, d.maxlen);
        if(d.len != d.maxlen){ ret = FALSE; goto rtn; }
        // simplest checksum
        uint16_t sum = 0;
        for(uint32_t i = 0; i < sizeof(SSconfig)-2; ++i) sum += d.buf[i];
        if(sum != conf->checksum){
            DBG("got sum: %u, need: %u", conf->checksum, sum);
            ret = FALSE;
            goto rtn;
        }
    }
rtn:
    pthread_mutex_unlock(&mntmutex);
    return ret;
}
