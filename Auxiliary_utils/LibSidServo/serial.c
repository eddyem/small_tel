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
static int encfd[2] = {-1, -1}, mntfd = -1;
// main mount data
static mountdata_t mountdata = {0};
// last encoders time and last encoders data - for speed measurement
static coordval_t lastXenc = {0}, lastYenc = {0};

// mutexes for RW operations with mount device and data
static pthread_mutex_t  mntmutex = PTHREAD_MUTEX_INITIALIZER,
                        datamutex = PTHREAD_MUTEX_INITIALIZER;
// encoders thread and mount thread
static pthread_t encthread, mntthread;
// max timeout for 1.5 bytes of encoder and 2 bytes of mount - for `select`
static struct timeval encRtmout = {0}, mntRtmout = {0};
// encoders raw data
typedef struct __attribute__((packed)){
    uint8_t magick;
    int32_t encY;
    int32_t encX;
    uint8_t CRC[4];
} enc_t;

/**
 * @brief dtime - monotonic time from first run
 * @return
 */
double dtime(){
    struct timespec start_time = {0}, cur_time;
    if(start_time.tv_sec == 0 && start_time.tv_nsec == 0){
        clock_gettime(CLOCK_MONOTONIC, &start_time);
    }
    clock_gettime(CLOCK_MONOTONIC, &cur_time);
    return ((double)(cur_time.tv_sec - start_time.tv_sec) +
                     (cur_time.tv_nsec - start_time.tv_nsec) * 1e-9);
}
#if 0
double dtime(){
    double t;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    t = tv.tv_sec + ((double)tv.tv_usec)/1e6;
    return t;
}
#endif
#if 0
double tv2d(struct timeval *tv){
    if(!tv) return 0.;
    double t = tv->tv_sec + ((double)tv->tv_usec) / 1e6;
    return t;
}
#endif
#if 0
// init start time
static void gttime(){
    struct timeval tv;
    gettimeofday(&tv, NULL);
    tv_sec_got = tv.tv_sec;
    tv_usec_got = tv.tv_usec;
}
#endif

// calculate current X/Y speeds
static void getXspeed(double t){
    mountdata.encXspeed.val = (mountdata.encXposition.val - lastXenc.val) / (t - lastXenc.t);
    mountdata.encXspeed.t = (lastXenc.t + t) / 2.;
    lastXenc.val = mountdata.encXposition.val;
    lastXenc.t = t;
}
static void getYspeed(double t){
    mountdata.encYspeed.val = (mountdata.encYposition.val - lastYenc.val) / (t - lastYenc.t);
    mountdata.encYspeed.t = (lastYenc.t + t) / 2.;
    lastYenc.val = mountdata.encYposition.val;
    lastYenc.t = t;
}

/**
 * @brief parse_encbuf - check encoder buffer (for encoder data based on SSII proto) and fill fresh data
 * @param databuf - input buffer with 13 bytes of data
 * @param t - time when databuf[0] got
 */
static void parse_encbuf(uint8_t databuf[ENC_DATALEN], double t){
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
    mountdata.encXposition.val = X_ENC2RAD(edata->encX);
    mountdata.encYposition.val = Y_ENC2RAD(edata->encY);
    DBG("Got positions X/Y= %.6g / %.6g", mountdata.encXposition.val, mountdata.encYposition.val);
    mountdata.encXposition.t = t;
    mountdata.encYposition.t = t;
    if(t - lastXenc.t > Conf.EncoderSpeedInterval) getXspeed(t);
    if(t - lastYenc.t > Conf.EncoderSpeedInterval) getYspeed(t);
    pthread_mutex_unlock(&datamutex);
    //DBG("time = %zd+%zd/1e6, X=%g deg, Y=%g deg", tv->tv_sec, tv->tv_usec, mountdata.encposition.X*180./M_PI, mountdata.encposition.Y*180./M_PI);
}

/**
 * @brief getencval - get uint64_t data from encoder
 * @param fd - encoder fd
 * @param val - value read
 * @param t - measurement time
 * @return amount of data read or 0 if problem
 */
static int getencval(int fd, double *val, double *t){
    if(fd < 0) return FALSE;
    char buf[128];
    int got = 0, Lmax = 127;
    double t0 = dtime();
    do{
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        struct timeval tv = encRtmout;
        int retval = select(fd + 1, &rfds, NULL, NULL, &tv);
        if(!retval) continue;
        if(retval < 0){
            if(errno == EINTR) continue;
            return 0;
        }
        if(FD_ISSET(fd, &rfds)){
            ssize_t l = read(fd, &buf[got], Lmax);
            if(l < 1) return 0; // disconnected ??
            got += l; Lmax -= l;
            buf[got] = 0;
        } else continue;
        if(strchr(buf, '\n')) break;
    }while(Lmax && dtime() - t0 < Conf.EncoderReqInterval);
    if(got == 0) return 0; // WTF?
    char *estr = strrchr(buf, '\n');
    if(!estr) return 0;
    *estr = 0;
    char *bgn = strrchr(buf, '\n');
    if(bgn) ++bgn;
    else bgn = buf;
    char *eptr;
    long data = strtol(bgn, &eptr, 10);
    if(eptr != estr){
        DBG("NAN");
        return 0; // wrong number
    }
    if(val) *val = (double) data;
    if(t) *t = t0;
    return got;
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
// read 1 byte from mount; return -1 if nothing to read, -2 if disconnected
static int getmntbyte(){
    if(mntfd < 0) return -1;
    uint8_t byte;
    fd_set rfds;
   /* ssize_t l = read(mntfd, &byte, 1);
    //DBG("MNT read=%zd byte=0x%X", l, byte);
    if(l == 0) return -1;
    if(l != 1) return -2; // disconnected ??
    return (int) byte;*/
    do{
        FD_ZERO(&rfds);
        FD_SET(mntfd, &rfds);
        struct timeval tv = mntRtmout;
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
static void *encoderthread1(void _U_ *u){
    if(Conf.SepEncoder != 1) return NULL;
    uint8_t databuf[ENC_DATALEN];
    int wridx = 0, errctr = 0;
    double t = 0.;
    while(encfd[0] > -1 && errctr < MAX_ERR_CTR){
        int b = getencbyte();
        if(b == -2) ++errctr;
        if(b < 0) continue;
        errctr = 0;
//        DBG("Got byte from Encoder: 0x%02X", b);
        if(wridx == 0){
            if((uint8_t)b == ENC_MAGICK){
//                DBG("Got magic -> start filling packet");
                databuf[wridx++] = (uint8_t) b;
                t = dtime();
            }
            continue;
        }else databuf[wridx++] = (uint8_t) b;
        if(wridx == ENC_DATALEN){
            parse_encbuf(databuf, t);
            wridx = 0;
        }
    }
    if(encfd[0] > -1){
        close(encfd[0]);
        encfd[0] = -1;
    }
    return NULL;
}

// main encoder thread for separate encoders as USB devices /dev/encoder_X0 and /dev/encoder_Y0
static void *encoderthread2(void _U_ *u){
    if(Conf.SepEncoder != 2) return NULL;
    DBG("Thread started");
    int errctr = 0;
    double t0 = dtime();
    const char *req = "next\n";
    int need2ask = 0; // need or not to ask encoder for new data
    while(encfd[0] > -1 && encfd[1] > -1 && errctr < MAX_ERR_CTR){
        if(need2ask){
            if(5 != write(encfd[0], req, 5)) { ++errctr; continue; }
            else if(5 != write(encfd[1], req, 5)) { ++errctr; continue; }
        }
        double v, t;
        if(getencval(encfd[0], &v, &t)){
            mountdata.encXposition.val = X_ENC2RAD(v);
            //DBG("encX(%g) = %g", t, mountdata.encXposition.val);
            mountdata.encXposition.t = t;
            if(t - lastXenc.t > Conf.EncoderSpeedInterval) getXspeed(t);
            if(getencval(encfd[1], &v, &t)){
                mountdata.encYposition.val = Y_ENC2RAD(v);
                //DBG("encY(%g) = %g", t, mountdata.encYposition.val);
                mountdata.encYposition.t = t;
                if(t - lastYenc.t > Conf.EncoderSpeedInterval) getYspeed(t);
                errctr = 0;
                need2ask = 0;
            } else {
                if(need2ask) ++errctr;
                else need2ask = 1;
                continue;
            }
        } else {
            if(need2ask) ++errctr;
            else need2ask = 1;
            continue;
        }
        while(dtime() - t0 < Conf.EncoderReqInterval){ usleep(10); }
        //DBG("DT=%g (RI=%g)", dtime()-t0, Conf.EncoderReqInterval);
        t0 = dtime();
    }
    DBG("ERRCTR=%d", errctr);
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
        double tgot = dtime();
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
        SSconvstat(status, &mountdata, tgot);
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
    DBG("Check speed: i=%d, o=%d", tty.c_ispeed, tty.c_ospeed);
    if(tty.c_ispeed != (speed_t) speed || tty.c_ospeed != (speed_t)speed){ close(fd); return -1; }
    // try to set exclusive
    if(ioctl(fd, TIOCEXCL)){DBG("Can't make exclusive");}
    return fd;
}

// return FALSE if failed
int openEncoder(){
    if(!Conf.SepEncoder) return FALSE; // try to open separate encoder when it's absent
    if(Conf.SepEncoder == 1){ // only one device
        DBG("One device");
        if(encfd[0] > -1) close(encfd[0]);
        encfd[0] = ttyopen(Conf.EncoderDevPath, (speed_t) Conf.EncoderDevSpeed);
        if(encfd[0] < 0) return FALSE;
        encRtmout.tv_sec = 0;
        encRtmout.tv_usec = 200000000 / Conf.EncoderDevSpeed; // 20 bytes
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
        encRtmout.tv_sec = 0;
        encRtmout.tv_usec = 1000; // 1ms
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
    if(mntfd > -1) close(mntfd);
    DBG("Open mount %s @ %d", Conf.MountDevPath, Conf.MountDevSpeed);
    mntfd = ttyopen(Conf.MountDevPath, (speed_t) Conf.MountDevSpeed);
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
    mntRtmout.tv_usec = 500000000 / Conf.MountDevSpeed; // 50 bytes
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
        DBG("join mount thread");
        pthread_join(mntthread, NULL);
        DBG("close fd");
        close(mntfd);
        mntfd = -1;
    }
    if(encfd[0] > -1){
        DBG("Kill encoder thread");
        pthread_cancel(encthread);
        DBG("join encoder thread");
        pthread_join(encthread, NULL);
        DBG("close fd");
        close(encfd[0]);
        encfd[0] = -1;
        if(Conf.SepEncoder == 2){
            close(encfd[1]);
            encfd[1] = -1;
        }
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

void setStat(mnt_status_t Xstatus, mnt_status_t Ystatus){
    pthread_mutex_lock(&datamutex);
    mountdata.Xstatus = Xstatus;
    mountdata.Ystatus = Ystatus;
    pthread_mutex_unlock(&datamutex);
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
    uint8_t buf[256];
    data_t dumb = {.buf = buf, .maxlen = 256};
    if(!in) in = &dumb; // even if user don't ask for answer, try to read to clear trash
    in->len = 0;
    for(size_t i = 0; i < in->maxlen; ++i){
        int b = getmntbyte();
        if(b < 0) break; // nothing to read -> go out
        in->buf[in->len++] = (uint8_t) b;
    }
    //DBG("Clear trashing input");
    while(getmntbyte() > -1);
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
// send binary data - without EOL
int MountWriteReadRaw(const data_t *out, data_t *in){
    pthread_mutex_lock(&mntmutex);
    int ret = wr(out, in, 0);
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
#ifdef EBUG
        logscmd((SSscmd*)cmd);
#endif
        if(!wr(dscmd, &a, 1)) goto rtn;
    }else if(len == sizeof(SSlcmd)){
        ((SSlcmd*)cmd)->checksum = SScalcChecksum(cmd, len-2);
        DBG("Long command");
#ifdef EBUG
        loglcmd((SSlcmd*)cmd);
#endif
        if(!wr(dlcmd, &a, 1)) goto rtn;
    }else{
        goto rtn;
    }
    DBG("Write %d bytes and wait for ans", len);
    data_t d;
    d.buf = cmd;
    d.len = d.maxlen = len;
    ret = wr(&d, NULL, 0);
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
// rw == 1 to write, 0 to read
int cmdC(SSconfig *conf, int rw){
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
        d.len = 0; d.maxlen = sizeof(SSconfig);
        ret = wr(rcmd, &d, 1);
        DBG("wr returned %s; got %zd bytes of %zd", ret ? "TRUE" : "FALSE", d.len, d.maxlen);
        if(d.len != d.maxlen) return FALSE;
        // simplest checksum
        uint16_t sum = 0;
        for(uint32_t i = 0; i < sizeof(SSconfig)-2; ++i) sum += d.buf[i];
        if(sum != conf->checksum){
            DBG("got sum: %u, need: %u", conf->checksum, sum);
            return FALSE;
        }
    }
rtn:
    pthread_mutex_unlock(&mntmutex);
    return ret;
}
