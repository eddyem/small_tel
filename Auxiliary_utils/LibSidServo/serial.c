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
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "dbg.h"
#include "serial.h"

// serial devices FD
static int encfd = -1, mntfd = -1;
// main mount data
static mountdata_t mountdata = {0};

// mutexes for RW operations with mount device and data
static pthread_mutex_t  mntmutex = PTHREAD_MUTEX_INITIALIZER,
                        datamutex = PTHREAD_MUTEX_INITIALIZER;
// encoders thread and mount thread
static pthread_t encthread, mntthread;
// max timeout for 1.5 bytes of encoder and 2 bytes of mount
static struct timeval encRtmout = {0}, mntRtmout = {0};
// encoders raw data
typedef struct __attribute__((packed)){
    uint8_t magick;
    int32_t encX;
    int32_t encY;
    uint8_t CRC[4];
} enc_t;

/**
 * @brief dtime - UNIX time with microsecond
 * @return value
 */
double dtime(){
    double t;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    t = tv.tv_sec + ((double)tv.tv_usec)/1e6;
    return t;
}

#if 0
// init start time
static void gttime(){
    struct timeval tv;
    gettimeofday(&tv, NULL);
    tv_sec_got = tv.tv_sec;
    tv_usec_got = tv.tv_usec;
}
#endif

/**
 * @brief parse_encbuf - check encoder buffer (for separate encoder) and fill fresh data
 * @param databuf - input buffer with 13 bytes of data
 * @param nexttime - time when databuf[0] got
 */
static void parse_encbuf(uint8_t databuf[ENC_DATALEN], struct timeval *tv){
    enc_t *edata = (enc_t*) databuf;
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
    mountdata.encposition.X = X_ENC2RAD(edata->encX);
    mountdata.encposition.Y = Y_ENC2RAD(edata->encY);
    mountdata.encposition.msrtime = *tv;
    pthread_mutex_unlock(&datamutex);
    DBG("time = %zd+%zd/1e6, X=%g deg, Y=%g deg", tv->tv_sec, tv->tv_usec, mountdata.encposition.X*180./M_PI, mountdata.encposition.Y*180./M_PI);
}

// try to read 1 byte from encoder; return -1 if nothing to read or -2 if device seems to be disconnected
static int getencbyte(){
    if(encfd < 0) return -1;
    uint8_t byte;
    fd_set rfds;
    struct timeval tv;
    do{
        FD_ZERO(&rfds);
        FD_SET(encfd, &rfds);
        tv = encRtmout;
        int retval = select(encfd + 1, &rfds, NULL, NULL, &tv);
        if(!retval) break;
        if(retval < 0){
            if(errno == EINTR) continue;
            return -1;
        }
        if(FD_ISSET(encfd, &rfds)){
            ssize_t l = read(encfd, &byte, 1);
            if(l != 1) return -2; // disconnected ??
            break;
        } else return -1;
    }while(1);
    return (int)byte;
}
// read 1 byte from mount; return -1 if nothing to read, -2 if disconnected
static int getmntbyte(){
    if(mntfd < 0) return -1;
    uint8_t byte;
    fd_set rfds;
    struct timeval tv;
   /* ssize_t l = read(mntfd, &byte, 1);
    //DBG("MNT read=%zd byte=0x%X", l, byte);
    if(l == 0) return -1;
    if(l != 1) return -2; // disconnected ??
    return (int) byte;*/
    do{
        FD_ZERO(&rfds);
        FD_SET(mntfd, &rfds);
        tv = mntRtmout;
        int retval = select(mntfd + 1, &rfds, NULL, NULL, &tv);
        if(retval < 0){
            if(errno == EINTR) continue;
            DBG("Error in select()");
            return -1;
        }
        //DBG("FD_ISSET = %d", FD_ISSET(mntfd, &rfds));
        if(FD_ISSET(mntfd, &rfds)){
            ssize_t l = read(mntfd, &byte, 1);
            //DBG("MNT read=%zd byte=0x%X", l, byte);
            if(l != 1) return -2; // disconnected ??
            break;
        } else return -1;
    }while(1);
    return (int)byte;
}

// main encoder thread (for separate encoder): read next data and make parsing
static void *encoderthread(void _U_ *u){
    uint8_t databuf[ENC_DATALEN];
    int wridx = 0, errctr = 0;
    struct timeval tv;
    while(encfd > -1 && errctr < MAX_ERR_CTR){
        int b = getencbyte();
        if(b == -2) ++errctr;
        if(b < 0) continue;
        errctr = 0;
        DBG("Got byte from Encoder: 0x%02X", b);
        if(wridx == 0){
            if((uint8_t)b == ENC_MAGICK){
                DBG("Got magic -> start filling packet");
                databuf[wridx++] = (uint8_t) b;
                gettimeofday(&tv, NULL);
            }
            continue;
        }else databuf[wridx++] = (uint8_t) b;
        if(wridx == ENC_DATALEN){
            parse_encbuf(databuf, &tv);
            wridx = 0;
        }
    }
    if(encfd > -1){
        close(encfd);
        encfd = -1;
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

// main mount thread
static void *mountthread(void _U_ *u){
    int errctr = 0;
    uint8_t buf[2*sizeof(SSstat)];
    SSstat *status = (SSstat*) buf;
    // data to get
    data_t d = {.buf = buf, .maxlen = sizeof(buf)};
    // cmd to send
    data_t *cmd_getstat = cmd2dat(CMD_GETSTAT);
    if(!cmd_getstat) goto failed;
    double t0 = dtime();
/*
#ifdef EBUG
    double t00 = t0;
#endif
*/
    while(mntfd > -1 && errctr < MAX_ERR_CTR){
        // read data to status
        struct timeval tgot;
        if(0 != gettimeofday(&tgot, NULL)) continue;
        if(!MountWriteRead(cmd_getstat, &d) || d.len != sizeof(SSstat)){
            DBG("Can't read SSstat, need %zd got %zd bytes", sizeof(SSstat), d.len);
            ++errctr; continue;
        }
        if(SScalcChecksum(buf, sizeof(SSstat)-2) != status->checksum){
            DBG("BAD checksum of SSstat, need %d", status->checksum);
            ++errctr; continue;
        }
        errctr = 0;
        pthread_mutex_lock(&datamutex);
        // now change data
        SSconvstat(status, &mountdata, &tgot);
        pthread_mutex_unlock(&datamutex);
        // allow writing & getters
        //DBG("t0=%g, tnow=%g", t0-t00, dtime()-t00);
        if(dtime() - t0 >= Conf.MountReqInterval) usleep(50);
        while(dtime() - t0 < Conf.MountReqInterval);
        t0 = dtime();
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
    if((fd = open(path, O_RDWR|O_NOCTTY)) < 0) return -1;
    if(ioctl(fd, TCGETS2, &tty)){ close(fd); return -1; }
    tty.c_lflag     = 0; // ~(ICANON | ECHO | ECHOE | ISIG)
    tty.c_iflag     = 0; // don't do any changes in input stream
    tty.c_oflag     = 0; // don't do any changes in output stream
    tty.c_cflag     = BOTHER | CS8 | CREAD | CLOCAL; // other speed, 8bit, RW, ignore line ctrl
    tty.c_ispeed = speed;
    tty.c_ospeed = speed;
    //tty.c_cc[VMIN]  = 0;  // non-canonical mode
    //tty.c_cc[VTIME] = 5;
    if(ioctl(fd, TCSETS2, &tty)){ close(fd); return -1; }
    DBG("Check speed");
    if(tty.c_ispeed != (speed_t) speed || tty.c_ospeed != (speed_t)speed){ close(fd); return -1; }
    // try to set exclusive
    if(ioctl(fd, TIOCEXCL)){DBG("Can't make exclusive");}
    return fd;
}

// return FALSE if failed
int openEncoder(const char *path, int speed){
    if(!Conf.SepEncoder) return FALSE; // try to open separate encoder when it's absent
    if(encfd > -1) close(encfd);
    encfd = ttyopen(path, (speed_t) speed);
    if(encfd < 0) return FALSE;
    encRtmout.tv_sec = 0;
    encRtmout.tv_usec = 200000000 / speed; // 20 bytes
    if(pthread_create(&encthread, NULL, encoderthread, NULL)){
        close(encfd);
        encfd = -1;
        return FALSE;
    }
    DBG("Encoder opened, thread started");
    return TRUE;
}

// return FALSE if failed
int openMount(const char *path, int speed){
    if(mntfd > -1) close(mntfd);
    DBG("Open mount %s @ %d", path, speed);
    mntfd = ttyopen(path, (speed_t) speed);
    if(mntfd < 0) return FALSE;
    DBG("mntfd=%d", mntfd);
    // clear buffer
    while(getmntbyte() > -1);
    /*int g = write(mntfd, "XXS\r", 4);
    DBG("Written %d", g);
    uint8_t buf[100];
    do{
        ssize_t l = read(mntfd, buf, 100);
        DBG("got %zd", l);
    }while(1);*/
    mntRtmout.tv_sec = 0;
    mntRtmout.tv_usec = 500000000 / speed; // 50 bytes
    if(pthread_create(&mntthread, NULL, mountthread, NULL)){
        DBG("Can't create thread");
        close(mntfd);
        mntfd = -1;
        return FALSE;
    }
    DBG("Mount opened, thread started");
    return TRUE;
}

// close all opened serial devices and quit threads
void closeSerial(){
    if(mntfd > -1){
        DBG("Kill mount thread");
        pthread_cancel(mntthread);
        DBG("close fd");
        close(mntfd);
        mntfd = -1;
    }
    if(encfd > -1){
        DBG("Kill encoder thread");
        pthread_cancel(encthread);
        DBG("close fd");
        close(encfd);
        encfd = -1;
    }
}

// get fresh encoder information
mcc_errcodes_t getMD(mountdata_t  *d){
    if(!d) return MCC_E_BADFORMAT;
    pthread_mutex_lock(&datamutex);
    *d = mountdata;
    pthread_mutex_unlock(&datamutex);
    return MCC_E_OK;
}

// write-read without locking mutex (to be used inside other functions)
static int wr(const data_t *out, data_t *in, int needeol){
    if((!out && !in) || mntfd < 0) return FALSE;
    if(out){
        if(out->len != (size_t)write(mntfd, out->buf, out->len)){
            DBG("written bytes not equal to need");
            return FALSE;
        }
        //DBG("Send to mount %zd bytes: %s", out->len, out->buf);
        if(needeol){
            int g = write(mntfd, "\r", 1); // add EOL
            (void) g;
        }
    }
    if(in){
        in->len = 0;
        for(size_t i = 0; i < in->maxlen; ++i){
            int b = getmntbyte();
            if(b < 0) break; // nothing to read -> go out
            in->buf[in->len++] = (uint8_t) b;
        }
    }
    return TRUE;
}

/**
 * @brief MountWriteRead - write and read @ once (or only read/write)
 * @param out (o) - data to write or NULL if not need
 * @param in  (i) - data to read or NULL if not need
 * @return FALSE if failed
 */
int MountWriteRead(const data_t *out, data_t *in){
    pthread_mutex_lock(&mntmutex);
    int ret = wr(out, in, 1);
    pthread_mutex_unlock(&mntmutex);
    return ret;
}

#ifdef EBUG
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
    static data_t *dscmd = NULL, *dlcmd = NULL;
    if(!dscmd) dscmd = cmd2dat(CMD_SHORTCMD);
    if(!dlcmd) dlcmd = cmd2dat(CMD_LONGCMD);
    int ret = FALSE;
    pthread_mutex_lock(&mntmutex);
    // dummy buffer to clear trash in input
    char ans[300];
    data_t a = {.buf = (uint8_t*)ans, .maxlen=299};
    if(len == sizeof(SSscmd)){
        ((SSscmd*)cmd)->checksum = SScalcChecksum(cmd, len-2);
        DBG("Short command");
        logscmd((SSscmd*)cmd);
        if(!wr(dscmd, &a, 1)) goto rtn;
    }else if(len == sizeof(SSlcmd)){
        ((SSlcmd*)cmd)->checksum = SScalcChecksum(cmd, len-2);
        DBG("Long command");
        loglcmd((SSlcmd*)cmd);
        if(!wr(dlcmd, &a, 1)) goto rtn;
    }else{
        goto rtn;
    }
    DBG("Write %d bytes and wait for ans", len);
    data_t d;
    d.buf = cmd;
    d.len = d.maxlen = len;
    ret = wr(&d, &d, 0);
#ifdef EBUG
    if(len == sizeof(SSscmd)) logscmd((SSscmd*)cmd);
    else loglcmd((SSlcmd*)cmd);
#endif
    DBG("%s", ret ? "SUCCESS" : "FAIL");
rtn:
    pthread_mutex_unlock(&mntmutex);
    return ret;
}

// return TRUE if OK
int cmdS(SSscmd *cmd){
    return bincmd((uint8_t *)cmd, sizeof(SSscmd));
}
int cmdL(SSlcmd *cmd){
    return bincmd((uint8_t *)cmd, sizeof(SSlcmd));
}
