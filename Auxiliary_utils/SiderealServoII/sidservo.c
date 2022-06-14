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

#include <stdio.h>
#include <string.h>

#include "sidservo.h"

static TTY_descr *dev = NULL; // shoul be global to restore if die
static uint8_t buff[BUFLEN];
static int buflen = 0;

static uint16_t calcChecksum(uint8_t *buf, int len){
    uint16_t checksum = 0x11; // I don't know from where does this "magick"
    for(int i = 0; i < len; i++)
        checksum += *buf++;
    checksum ^= 0xFF00; // invert high byte
    DBG("Checksum: 0x%04x", checksum);
    return checksum;
}

/**
 * @brief SSinit - open serial device and get initial info
 * @return TRUE if all OK
 */
int SSinit(char *devpath, int speed){
    LOGDBG("Try to open serial %s @%d", devpath, speed);
    dev = new_tty(devpath, speed, BUFLEN);
    if(dev) dev = tty_open(dev, 1); // open exclusively
    if(!dev){
        LOGERR("Can't open %s with speed %d. Exit.", devpath, speed);
        signals(-1);
    }
    for(int ntries = 0; ntries < 5; ++ntries){ // try at most 5 times
        if(!SSgetstat(NULL)) continue;
        SSstat *s = (SSstat*) buff;
#ifdef EBUG
        green("\nGet data:\n");
        printf("DECmot=%d (0x%08x)\n", s->DECmot, (uint32_t)s->DECmot);
        printf("DECenc=%d (0x%08x)\n", s->DECenc, (uint32_t)s->DECenc);
        printf("RAmot=%d (0x%08x)\n", s->RAmot, (uint32_t)s->RAmot);
        printf("RAenc=%d (0x%08x)\n", s->RAenc, (uint32_t)s->RAenc);
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
#endif
        if(calcChecksum(buff, sizeof(SSstat)-2) == s->checksum) return TRUE;
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
 * @brief SSwrite - write & return answer
 * @param buf    - buffer with text or binary data
 * @param buflen - its length
 * @return amount of bytes read, if got answer; 0 without answer, -1 if device disconnected, -2 if can't write
 */
int SSwrite(const uint8_t *buf, int len){
    DBG("try to write %d bytes", len);
    if(write_tty(dev->comfd, (const char*)buf, len)){
        LOGERR("Can't write data to port");
        return -2;
    }
    write_tty(dev->comfd, "\r", 1); // add EOL
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
        if((int)dev->buflen > rest) dev->buflen = rest; // TODO: what to do with possible buffer overrun?
        memcpy(&buff[buflen], dev->buf, dev->buflen);
        buflen += dev->buflen;
    }
    DBG("got buflen=%d", buflen);
#ifdef EBUG
    for(int i = 0; i < buflen; ++i){
        printf("%02x (%c) ", buff[i], (buff[i] > 31) ? buff[i] : ' ');
    }
    printf("\n");
#endif
    return buflen;
}

/**
 * @brief SSread - return buff and buflen
 * @param l (o) - length of data
 * @return buff or NULL if buflen == 0
 */
uint8_t *SSread(int *l){
    if(l) *l = buflen;
    if(!buflen) return NULL;
    return buff;
}

/**
 * @brief SSgetstat - get struct with status & check its crc
 * @param s - pointer to allocated struct (or NULL just to check)
 * @return 1 if OK
 */
int SSgetstat(SSstat *s){
    if(SSwrite(CMD_GETSTAT, sizeof(CMD_GETSTAT)) != sizeof(SSstat)) return FALSE;
    if(s) memcpy(s, buff, sizeof(SSstat));
    return TRUE;
}
