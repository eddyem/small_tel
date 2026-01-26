/*
 * This file is part of the Hydreon_RG11 project.
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

#include <string.h>

#include "hydreon.h"

static sl_tty_t *dev = NULL;

// regular registers names
static const char* rregnames[RREGNUM] = {
    "PeakRS",
    "SPeakRS",
    "RainAD8",
    "LRA",
    "TransRat",
    "AmbLNoise"
};

// rgbit names
static const char* rgbitnames[RGBITNUM] = {
    "PkOverThr",
    "Raining",
    "Out1On",
    "HtrOn",
    "IsDark",
    "Cndnstn",
    "Freeze",
    "Storm"
};

static const char *slowregnames[SREGNUM] = {
    "RevLevel",
    "EmLevel",
    "RecEmStr",
    "ABLevel",
    "TmprtrF",
    "PUGain",
    "ClearTR",
    "AmbLight",
    "Bucket",
    "Barrel",
    "RGConfig",
    "DwellT",
    "SinceRn",
    "MonoStb",
    "LightAD",
    "RainThr",
};

const char *regname(int N){
    if(N < 0 || N > RREGNUM) return NULL;
    return rregnames[N];
}
const char *rgbitname(int N){
    if(N < 0 || N > RGBITNUM) return NULL;
    return rgbitnames[N];
}
const char *slowname(int N){
    if(N < 0 || N > SREGNUM) return NULL;
    return slowregnames[N];
}

static int getv(char s, uint8_t *v){
    if(s >= '0' && s <= '9'){
        *v = s - '0';
        return 1;
    }else if(s >= 'a' && s <= 'f'){
        *v = 10 + s - 'a';
        return 1;
    }
    return 0;
}

static int encodepacket(const char *buf, int len, rg11 *Rregs, slowregs *Sregs){
    uint8_t databuf[REGLEN/2] = {0};
    static slowregs slow = {0};
    if(len != REGMINLEN && len != REGLEN) return FALSE;
    for(int i = 0; i < len; ++i){
        int l = i&1; // low part
        int idx = i/2; // data index
        uint8_t v;
        if(!getv(buf[i], &v)) return 0;
        if(l) databuf[idx] |= v;
        else databuf[idx] |= v << 4;
    }
    if(Rregs) memcpy(Rregs, databuf, sizeof(rg11));
    rg11 r = *((rg11*)databuf);
    uint8_t *s = (uint8_t*) &slow;
    if(len == REGLEN){
        if(r.SlowRegIngex < 16){
            s[r.SlowRegIngex] = r.SlowRegValue;
        }
    }
    if(Sregs) memcpy(Sregs, &slow, sizeof(slowregs));
    return TRUE;
}

/**
 * @brief getpacket - try to read next data in packet
 * @param Rregs (o) - regular registers (if return TRUE)
 * @param Sregs (o) - slow registers (if return TRUE)
 * @return FALSE if it is still no data @ input
 */
int hydreon_getpacket(rg11 *Rregs, slowregs *Sregs){
    if(!dev) return 0;
    static int buflen = 0;
    static char strbuf[BUFLEN];
    int l = sl_tty_read(dev);
    if(l < 1) return FALSE;
    char s = dev->buf[0];
    if(s == 's'){ // start of new packet -> encode old
        if(buflen){
            l = encodepacket(strbuf, buflen, Rregs, Sregs);
            buflen = 0;
            return l;
        }
    }else{
        strbuf[buflen++] = s;
        if(buflen >= BUFLEN){
            WARNX("Buffer overfull");
            buflen = 0;
        }
    }
    return FALSE;
}

/**
 * @brief open_hydreon - open serial device
 * @param devname (i) - device path
 * @return TRUE or FALSE if failed
 */
int hydreon_open(const char *devname){
    dev = sl_tty_new((char*)devname, 1200, 1);
    if(!dev) return FALSE;
    dev = sl_tty_open(dev, 1);
    if(!dev) return FALSE;
    return TRUE;
}

void hydreon_close(){
    if(dev) sl_tty_close(&dev);
}
