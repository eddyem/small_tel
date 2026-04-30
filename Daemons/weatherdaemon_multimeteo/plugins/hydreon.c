/*
 * This file is part of the weatherdaemon project.
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

#include <string.h>
#include <time.h>

#include "weathlib.h"

// HYDREON rain sensor

#define SENSOR_NAME "Hydreon RG-11 rain sensor"

// amount of datafields
#define RREGNUM     6
#define RGBITNUM    8
#define SREGNUM     16

// RGBits values:
// PeakRS overflow (>255)
#define PkOverThr   (1<<0)
// is raining (after several PKOverThr by fixed time)
#define Raining     (1<<1)
// outern relay is on (after bucket overflows from 18 to 0)
#define Out1On      (1<<2)
// heater is on
#define HtrOn       (1<<3)
// ambient light @0 (murky, twilight)
#define IsDark      (1<<4)
// ???
#define Cndnstn     (1<<5)
// ???
#define Freeze      (1<<6)
// ???
#define Storm       (1<<7)

// minimal packet length (without slow registers)
#define REGMINLEN   (14)
// standard packet length
#define REGLEN      (18)
#define BUFLEN      (32)

typedef struct{
    uint8_t PeakRS;         // water intensity (255 - continuous)
    uint8_t SPeakRS;        // most time == PeakRS
    uint8_t RainAD8;        // (???)
    uint8_t LRA;            // average rain activity (~envelope of PeakRS)
    uint8_t TransRat;       // amount of measurements per second (???)
    uint8_t AmbLNoise;      // ambient noise RMS (???)
    uint8_t RGBits;         // flags
    uint8_t SlowRegIngex;   // slow register index
    uint8_t SlowRegValue;   // slow register value
} rg11;

typedef struct{
    uint8_t RevLevel;     // (??? == 14)
    uint8_t EmLevel;      // (???) seems correlated with RainAD8
    uint8_t RecEmStr;     // (???) seems correlated with RainAD8
    uint8_t ABLevel;      // (??? == 7..12)
    uint8_t TmprtrF;      // (inner T)
    uint8_t PUGain;       // (??? == 37)
    uint8_t ClearTR;      // (??? almost constant == 121..149)
    uint8_t AmbLight;     // ambient light
    uint8_t Bucket;       // Intergal PeakRS. When no rain, decreased near 4 hours per 1 unit
    uint8_t Barrel;       // Integral Bucket (increases when Bucket goes through 12->14 after last overflow). Decreased near 2 hours per 1 unit
    uint8_t RGConfig;     // (??? == 0)
    uint8_t DwellT;       // 100 - no rain, 50 - low, 5 - max rain (like exponental function)
    uint8_t SinceRn;      // (0..20) increases every minute after rain is over
    uint8_t MonoStb;      // when Raining==1, MonoStb=15, then decrements when no rain (1 unit per ~1minute)
    uint8_t LightAD;      // (???) seems correlated with RainAD8
    uint8_t RainThr;      // (??? == 12)
} slowregs;

enum{
    NPRECIP = 0,
    NPRECIP_LEVEL,
    NSINCERN,
    NPOW,
    NAVG,
    NAMBL,
    NFREEZ,
    NAMOUNT
};

static const val_t values[NAMOUNT] = { // fields `name` and `comment` have no sense until value meaning is `IS_OTHER`
    [NPRECIP] = {.sense = VAL_OBLIGATORY,  .type = VALT_UINT, .meaning = IS_PRECIP},
    [NPRECIP_LEVEL] = {.sense = VAL_RECOMMENDED, .type = VALT_FLOAT, .meaning = IS_PRECIP_LEVEL},
    [NSINCERN] = {.sense = VAL_UNNECESSARY, .type = VALT_UINT, .meaning = IS_OTHER, .name = "TSINCERN", .comment = "Minutes since rain (20 means a lot of)"},
    [NPOW] = {.sense = VAL_UNNECESSARY, .type = VALT_UINT, .meaning = IS_OTHER, .name = "RAINPOW", .comment = "Rain strength, 0..255"},
    [NAVG] = {.sense = VAL_UNNECESSARY, .type = VALT_UINT, .meaning = IS_OTHER, .name = "RAINAVG", .comment = "Average rain strength, 0..255"},
    [NAMBL] = {.sense = VAL_UNNECESSARY, .type = VALT_UINT, .meaning = IS_OTHER, .name = "RSAMBL", .comment = "Ambient light by rain sensor, 0..255"},
    [NFREEZ] = {.sense = VAL_UNNECESSARY, .type = VALT_UINT, .meaning = IS_OTHER, .name = "RSFREEZ", .comment = "Rain sensor is freezed"},
};

static int getv(char s, uint8_t *v){
    if(s >= '0' && s <= '9'){
        *v = s - '0';
        return 1;
    }else if(s >= 'a' && s <= 'f'){
        *v = 10 + s - 'a';
        return 1;
    }
    DBG("'%c' not a HEX", s);
    return 0;
}

static int encodepacket(const char *buf, int len, rg11 *Rregs, slowregs *Sregs){
    DBG("got buffer: %s[%d]", buf, len);
    uint8_t databuf[REGLEN/2] = {0};
    static slowregs slow = {0};
    if(len != REGMINLEN && len != REGLEN){
        DBG("Wrong buffer len!");
        return FALSE;
    }
    for(int i = 0; i < len; ++i){
        int l = i&1; // low part
        int idx = i/2; // data index
        uint8_t v;
        if(!getv(buf[i], &v)) return FALSE;
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

static void *mainthread(void *s){
    FNAME();
    char buf[128];
    rg11 Rregs;
    slowregs Sregs;
    sensordata_t *sensor = (sensordata_t *)s;
    while(sensor->fdes > -1){
        time_t tnow = time(NULL);
        int canread = sl_canread(sensor->fdes);
        if(canread < 0){
            WARNX("Disconnected fd %d", sensor->fdes);
            break;
        }else if(canread == 1){
            ssize_t got = read(sensor->fdes, buf, 128);
            if(got > 0){
                //DBG("write into buffer: %s[%zd]", buf, got);
                sl_RB_write(sensor->ringbuffer, (uint8_t*)buf, got);
            }else if(got < 0){
                DBG("Disconnected?");
                break;
            }
        }
        int got = sl_RB_readto(sensor->ringbuffer, 's', (uint8_t*)buf, 127);
        if(got > 0){
            buf[--got] = 0;
            if(encodepacket(buf, got, &Rregs, &Sregs)){
                DBG("refresh...");
                pthread_mutex_lock(&sensor->valmutex);
                for(int i = 0; i < NAMOUNT; ++i)
                    sensor->values[i].time = tnow;
                sensor->values[NPRECIP].value.u = (Rregs.RGBits & (Raining | Storm)) ? 1 : 0;
                float f = Sregs.Barrel * 256.f + Sregs.Bucket - 14.f;
                sensor->values[NPRECIP_LEVEL].value.f = (f > 0.f) ? f : 0.f;
                sensor->values[NSINCERN].value.u = Sregs.SinceRn;
                sensor->values[NPOW].value.u = Rregs.PeakRS;
                sensor->values[NAVG].value.u = Rregs.LRA;
                sensor->values[NAMBL].value.u = Sregs.AmbLight;
                sensor->values[NFREEZ].value.u = (Rregs.RGBits & Freeze) ? 1 : 0;
                pthread_mutex_unlock(&sensor->valmutex);
                if(sensor->freshdatahandler) sensor->freshdatahandler(sensor);
            }
        }
    }
    DBG("OOOOps!");
    sensor->kill(sensor);
    return NULL;
}

sensordata_t *sensor_new(int N, time_t pollt, const char *descr){
    FNAME();
    if(!descr || !*descr) return NULL;
    int fd = getFD(descr);
    if(fd < 0) return NULL;
    sensordata_t *s = common_new();
    if(!s) return NULL;
    snprintf(s->name, NAME_LEN, "%s @ %s", SENSOR_NAME, descr);
    s->fdes = fd;
    s->PluginNo = N;
    s->Nvalues = NAMOUNT;
    if(pollt) s->tpoll = pollt;
    s->values = MALLOC(val_t, NAMOUNT);
    // don't use memcpy, as `values` could be aligned
    for(int i = 0; i < NAMOUNT; ++i) s->values[i] = values[i];
    if(!(s->ringbuffer = sl_RB_new(BUFSIZ)) ||
        pthread_create(&s->thread, NULL, mainthread,  (void*)s)){
        s->kill(s);
        return NULL;
    }
    return s;
}
