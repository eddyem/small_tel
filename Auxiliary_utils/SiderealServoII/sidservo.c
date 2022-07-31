/*
 * This file is part of the SSII project.
 * Copyright 2022 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "emulator.h"
#include "motlog.h"
#include "sidservo.h"

static TTY_descr *dev = NULL; // shoul be global to restore if die
static uint8_t buff[BUFLEN];
static int buflen = 0;

static uint16_t calcChecksum(uint8_t initval, uint8_t *buf, int len){
    uint16_t checksum = initval; // I don't know from where does this "magick"
    for(int i = 0; i < len; i++)
        checksum += *buf++;
    checksum ^= 0xFF00; // invert high byte
    DBG("Checksum of %d bytes: 0x%04x", len, checksum);
    return checksum;
}

// encoder's settings per rev
static struct {
    uint32_t hamotperrev;   // motors' encoders ticks per revolution
    uint32_t decmotperrev;
    uint32_t haencperrev;   // axis' encoders ticks per revolution
    uint32_t decencperrev;
    int32_t hamotzero;      // motors' encoders ticks @ zero position
    int32_t decmotzero;
} encsettings;

/**
 * @brief SSinit - open serial device and get initial info
 * @return TRUE if all OK
 */
int SSinit(char *devpath, int speed){
    LOGDBG("Try to open serial %s @%d", devpath, speed);
    int64_t i;
    dev = new_tty(devpath, speed, BUFLEN);
    if(dev) dev = tty_open(dev, 1); // open exclusively
    if(!dev){
        LOGERR("Can't open %s with speed %d. Exit.", devpath, speed);
        signals(-1);
    }
    for(int ntries = 0; ntries < 10; ++ntries){ // try at most 10 times
        SSstat s;
        DBG("Try for %dth time... ", ntries);
        SSwritecmd(CMD_MOTHA);
        SSwritecmd(CMD_MOTDEC);
        if(!SSgetPartialstat(&s)) continue;
        if(!SSgetInt(CMD_GETDECMEPR, &i)) continue;
        encsettings.decmotperrev = (uint32_t) i;
        LOGDBG("decmotperrev = %u", encsettings.decmotperrev);
        if(!SSgetInt(CMD_GETHAMEPR, &i)) continue;
        encsettings.hamotperrev = (uint32_t) i;
        LOGDBG("hamotperrev = %u", encsettings.hamotperrev);
        if(!SSgetInt(CMD_GETDECAEPR, &i)) continue;
        encsettings.decencperrev = (uint32_t) i;
        LOGDBG("decencperrev = %u", encsettings.decencperrev);
        if(!SSgetInt(CMD_GETHAAEPR, &i)) continue;
        encsettings.haencperrev = (uint32_t) i;
        LOGDBG("haencperrev = %u", encsettings.haencperrev);
        double d = (double)(s.HAenc - HA_ENC_ZEROPOS);
        d /= encsettings.haencperrev;
        d *= encsettings.hamotperrev;
        encsettings.hamotzero = s.HAmot - (int32_t)d;
        LOGDBG("hamotzero = %d", encsettings.hamotzero);
        d = (double)(s.DECenc - DEC_ENC_ZEROPOS);
        d /= encsettings.decencperrev;
        d *= encsettings.decmotperrev;
        encsettings.decmotzero = s.DECmot - (int32_t)d;
        LOGDBG("decmotzero = %u", encsettings.decmotzero);
        return TRUE;
    }
    return FALSE;
}

/**
 * @brief SSclose - stop telescope and close serial device
 */
void SSclose(){
    if(dev) close_tty(&dev);
}

/**
 * @brief SSwrite - write command and wait for answer
 * @param buf    - buffer with text or binary data
 * @param len - its length
 * @param flags - >0: wait for binary data (until timeout), ==0: wait for '\r' or timeout, <0: don't wait at all
 * @return amount of bytes read, if got answer; 0 without answer, -1 if device disconnected, -2 if can't write
 */
int SSwrite(const uint8_t *buf, int len, int flags){
    DBG("try to write %d bytes", len);
#ifdef EBUG
    double t00 = dtime();
    for(int i = 0; i < len; ++i) if(buf[i] > 32) printf("%c", buf[i]); else printf(".");
    printf("\n");
    for(int i = 0; i < len; ++i) printf("0x%02x ", buf[i]);
    printf("\n");
#endif
    read_tty(dev); // clear incoming buffer
    if(write_tty(dev->comfd, (const char*)buf, (size_t) len)){
        LOGERR("Can't write data to port");
        DBG("Can't write, time=%g", dtime()-t00);
        return -2;
    }
    write_tty(dev->comfd, "\r", 1); // add EOL
    if(flags < 0){
        DBG("Don't wait, time=%g", dtime()-t00);
        return 0;
    }
    buflen = 0;
    double t0 = dtime();
    while(dtime() - t0 < READTIMEOUT && buflen < BUFLEN){
        int r = read_tty(dev);
        if(r == -1){
            LOGERR("Seems like tty device disconnected");
            return -1;
        }
        if(r == 0) continue;
        int rest = BUFLEN - buflen;
        int out = 0;
        DBG("buf[%d]=0x%02x", r-1, dev->buf[r-1]);
        if(flags == 0 && dev->buf[r-1] == '\n') out = 1; // end of message
        if((int)dev->buflen > rest){
            dev->buflen = (size_t) rest;
            out = 1;
        }
        memcpy(&buff[buflen], dev->buf, dev->buflen);
        buflen += dev->buflen;
        DBG("get %ld bytes, out=%d", dev->buflen, out);
        if(out) break;
        t0 = dtime();
    }
    DBG("got buflen=%d, time=%g", buflen, dtime()-t00);
#ifdef EBUG
    for(int i = 0; i < buflen; ++i){
        printf("%02x (%c) ", buff[i], (buff[i] > 31) ? buff[i] : ' ');
    }
    printf("\n");
#endif
    return buflen;
}

// write simple command with string answer
int SSwritecmd(const uint8_t *cmd){
    return SSwrite(cmd, (int)strlen((const char*)cmd), 0);
}

// write command and get answer
int SSgetInt(const uint8_t *cmd, int64_t *ans){
    int r = SSwritecmd(cmd);
    if(r > 0 && ans){
        int l = 0;
        char *ptr = (char*) SSread(&l);
        ptr[l] = 0;
        while(l){
            if(isdigit(*ptr)) break;
            ++ptr; ++l;
        }
        if(l == 0) return 0; // no integer found
        *ans = atol(ptr);
        DBG("from %s get answer: %ld", buff, *ans);
    }
    return r;
}

/**
 * @brief SSread - return buff and buflen
 * @param l (o) - length of data
 * @return buff or NULL if buflen == 0
 */
uint8_t *SSread(int *l){
    if(!buflen) return NULL;
    if(l) *l = buflen;
    return buff;
}

// fill some fields of `s` by stupid string commands
int SSgetPartialstat(SSstat *s){
    SSstat st = {0};
    int64_t par;
    int r = 0;
#define GET(cmd, field)   do{if(SSgetInt(cmd, &par)){st.field = (int32_t)par; ++r;}}while(0)
    GET(CMD_MOTDEC, DECmot);
    GET(CMD_MOTHA, HAmot);
    GET(CMD_ENCDEC, DECenc);
    GET(CMD_ENCHA, HAenc);
#undef GET
    if(s) *s = st;
#ifdef EBUG
    if(s){
        green("\nGet data:\n");
        printf("DECmot=%d (0x%08x)\n", s->DECmot, (uint32_t)s->DECmot);
        printf("DECenc=%d (0x%08x)\n", s->DECenc, (uint32_t)s->DECenc);
        printf("RAmot=%d (0x%08x)\n", s->HAmot, (uint32_t)s->HAmot);
        printf("RAenc=%d (0x%08x)\n", s->HAenc, (uint32_t)s->HAenc);
    }
#endif
    return r; // amount of fields with good data
}

static void parsestat(SSstat *s){
    green("\nGet data:\n");
    printf("DECmot=%d (0x%08x)\n", s->DECmot, (uint32_t)s->DECmot);
    printf("DECenc=%d (0x%08x)\n", s->DECenc, (uint32_t)s->DECenc);
    printf("DECLast=%d (0x%08x)\n", s->DecLast, (uint32_t)s->DecLast);
    printf("RAmot=%d (0x%08x)\n", s->HAmot, (uint32_t)s->HAmot);
    printf("RAenc=%d (0x%08x)\n", s->HAenc, (uint32_t)s->HAenc);
    printf("RALast=%d (0x%08x)\n", s->HALast, (uint32_t)s->HALast);
    printf("keypad=0x%02x\n", s->keypad);
    printf("XBits=0x%02x\n", s->XBits);
    printf("YBits=0x%02x\n", s->YBits);
    printf("EBits=0x%02x\n", s->ExtraBits);
    printf("ain0=%u\n", s->ain0);
    printf("ain1=%u\n", s->ain1);
    printf("millis=%u\n", s->millis);
    printf("tF=%d\n", s->tF);
    printf("V=%f\n", ((float)s->voltage)/10.f);
    printf("checksum=0x%04x\n\n", s->checksum);
}

/**
 * @brief SSgetstat - get struct with status & check its crc
 * @param s - pointer to allocated struct (or NULL just to check)
 * @return 1 if OK
 */
int SSgetstat(SSstat *s){
    int r = SSwrite(CMD_GETSTAT, 3, 1);
    SSstat *n;
    switch(r){
        case sizeof(SSstat):
            n = (SSstat*) buff;
            if(calcChecksum(0, buff, sizeof(SSstat)-2) != n->checksum) return FALSE;
            if(s) memcpy(s, buff, sizeof(SSstat));
        break;
        default: return FALSE; // wrong answer size
    }
#ifdef EBUG
    if(s){
        parsestat(s);
    }
#endif
    return TRUE;
}

// send short/long binary command
static int bincmd(void *cmd, int len){
    //uint8_t *buf = MALLOC(uint8_t, len + 3);
    if(len == sizeof(SSscmd)){
        ((SSscmd*)cmd)->checksum = calcChecksum(0, cmd, len-2);
        SSwrite(CMD_SHORTCMD, 3, -1);
        //memcpy(buf, CMD_SHORTCMD, 3);
        //memcpy(buf+3, cmd, len);
    }else if(len == sizeof(SSlcmd)){
        ((SSlcmd*)cmd)->checksum = calcChecksum(0, cmd, len-2);
        SSwrite(CMD_LONGCMD, 3, -1);
        //memcpy(buf, CMD_LONGCMD, 3);
        //memcpy(buf+3, cmd, len);
    }else{
        //free(buf);
        return -1;
    }
    //len += 3;
    DBG("Write %d bytes", len);
    int r = SSwrite(cmd, len, 1);
#ifdef EBUG
    if(r == sizeof(SSscmd)) parsestat((SSstat*)buff);
#endif
    return r;
}
int SScmds(SSscmd *cmd){
    int r = bincmd(cmd, sizeof(SSscmd));
    if(r < 0) return -1;
    DBG("Got %d bytes answer", r);
    return r;
}
int SScmdl(SSlcmd *cmd){
    int r = bincmd(cmd, sizeof(SSlcmd));
    if(r < 0) return -1;
    DBG("Got %d bytes answer", r);
    return r;
}

// return d in 0..360 for isdec==1 and -180..180 for isdec==0
static double normangle(double d, int isdec){
    if(d < -360.) d = fmod((fmod(d, 360.) + 360.), 360.);
    else d = fmod(d + 360., 360.);
    if(isdec) return d;
    if(d < 180.) return d;
    return (d - 180.);
}

/**
 * @brief SSticks2deg - convert motor ticks to degrees
 * @param ticks - motor ticks
 * @param isdec == 1 if it is DEC axe
 * @return degrees
 */
double SSticks2deg(int32_t ticks, int isdec){
    double denom = (isdec) ? encsettings.decmotperrev : encsettings.hamotperrev;
    int32_t zp = (isdec) ? encsettings.decmotzero : encsettings.hamotzero;
    return normangle(360. * (ticks - zp) / denom, isdec);
}
/**
 * @brief SSenc2deg - convert encoder ticks to degrees
 * @param ticks - motor ticks
 * @param isdec == 1 if it is DEC axe
 * @return degrees
 */
double SSenc2deg(int32_t ticks, int isdec){
    double denom = (isdec) ? encsettings.decencperrev : encsettings.haencperrev;
    int32_t zp = (isdec) ? DEC_ENC_ZEROPOS : HA_ENC_ZEROPOS;
    return normangle(360. * (ticks - zp) / denom, isdec);
}

/**
 * @brief SSdeg2ticks - convert degrees to motor ticks
 * @param d - angle
 * @param isdec == 1 if it is DEC axe
 * @return ticks
 */
int32_t SSdeg2ticks(double d, int isdec){
    double k = (isdec) ? encsettings.decmotperrev : encsettings.hamotperrev;
    int32_t zp = (isdec) ? encsettings.decmotzero : encsettings.hamotzero;
    return (int32_t)(k * normangle(d, isdec) / 360.) + zp;

}
/**
 * @brief SSdeg2enc - convert degrees to encoder ticks
 * @param d - angle
 * @param isdec == 1 if it is DEC axe
 * @return ticks
 */
int32_t SSdeg2enc(double d, int isdec){
    double k = (isdec) ? encsettings.decencperrev : encsettings.haencperrev;
    int32_t zp = (isdec) ? DEC_ENC_ZEROPOS : HA_ENC_ZEROPOS;
    return (int32_t)(k * normangle(d, isdec) / 360.) + zp;
}

// convert speed `dps` in degrees per second into ticks for `XS`/`YS` commands
int32_t SSdeg2spd(double dps, int isdec){
    double k = (isdec) ? encsettings.decmotperrev : encsettings.hamotperrev;
    return (int32_t)(k * dps * 65536./1953./360.);
}

// convert ticks for `XS`/`YS` into degrees per second
double SSspd2deg(int32_t ticks, int isdec){
    ;
}

/**
 * @brief SSgoto - move telescope to given angle
 * @param ha - Hour Angle (degrees)
 * @param dec - Declination (degrees)
 * @return TRUE if OK
 */
int SSgoto(double ha, double dec){
    char buf[32];
    if(ha < 0. || ha > 360.) return FALSE;
    if(dec < -90. || dec > 90.) return FALSE;
    int32_t raticks = SSdeg2ticks(ha, 0);
    int32_t decticks = SSdeg2ticks(dec, 1);
    snprintf(buf, 31, "%s%u", CMD_MOTDEC, decticks);
    SSwritecmd((uint8_t*)buf);
    snprintf(buf, 31, "%s%u", CMD_MOTHA, raticks);
    SSwritecmd((uint8_t*)buf);
    return TRUE;
}

// wait till moving ends
void SSwaitmoving(){
    SSstat s = {0};
    double t0 = dtime();
    int32_t oldRAm = 0, oldDm = 0;
    int first = 1, ctr = 0;
    do{
        if(!SSlog_motor_data(&s, &t0)) continue;
        DBG("Moving: HA=%g, DEC=%g", SSenc2deg(s.HAenc, 0), SSenc2deg(s.DECenc, 1));
        if(first) first = 0;
        else if(s.HAmot == oldRAm && s.DECmot == oldDm){
            if(++ctr > 2) return;
        }else ctr = 0;
        oldRAm = s.HAmot;
        oldDm = s.DECmot;
    }while(dtime() - t0 < 3.); // simplest timeout
    DBG("Moving ends (or timeout ends)");
}

int SScatchtarg(double ra, double dec){
    ;
}

#if 0
The controller loops 1953 times per second. Fields that reference a speed/velocity/rate (e.g. the XS command) express this speed as a 32-bit number representing the number of ticks the motor encoder should advance per loop, multiplied by 2^16 (= 65,536) to express fractional values.

For example, if you want to advance the motor 1,000 counts per second, this corresponds to:

1,000 / 1953 ~= 0.512 counts per loop

which is expressed as the integer value:

        round(0.512 * 65,536) = 33,555

So you would send the command ?XS33555\r? to set a max speed of 1,000 counts per second.

Note that 65,536 / 1,953 = 33.55657962109575, and 1,953 / 65,536 = 0.0298004150390625, so you can simply do:

        CountsPerSecToSpeedValue(cps): return round(cps * 33.55657962109575)

        SpeedValueToCountsPerSec(speed): return round(speed * 0.0298004150390625)

The SiTech Operations Manual describes the following ?C++? functions (converted to something like Python here) to do similar conversions:

        def DegsPerSec2MotorSpeed(dps, ticksPerRev):

return round(ticksPerRev * dps * 0.09321272116971)

def MotorSpeed2DegsPerSec(speed, ticksPerRev):

        return round(speed / ticksPerRev * 10.7281494140625)

These coefficients can be derived as follows:

        10.7281494140625 = 0.0298004150390625 * 360 degs/rev

        0.09321272116971 = 33.55657962109575 / 360 degs/rev

#endif
