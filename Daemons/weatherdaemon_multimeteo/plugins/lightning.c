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

/**
 * Plugin for AS3935-based lightning sensor
 * https://github.com/eddyem/stm32samples/tree/master/F1:F103/AS3935-lightning
 **/

#include <string.h>
#include "weathlib.h"

#define SENSOR_NAME "AS3935 lightning sensor"
// minimal distance for forced shutdown
#define MINDIST     (5.0)
// time to check wether sensor is alive, seconds
#define TCHECK      (30)

// indexes for text commands and answers
enum{
    CMD_INTERRUPT,
    CMD_ENERGY,
    CMD_DISTANCE,
    ANS_LIGHTNING,
    ANS_NOICE,
    ANS_DISTURBER,
    CMD_AMOUNT
};

static const char * commands[CMD_AMOUNT] = {
    [CMD_INTERRUPT] = "INTERRUPT",
    [CMD_ENERGY]    = "energy",
    [CMD_DISTANCE]  = "distance",
    [ANS_LIGHTNING] = "LIGHTNING",
    [ANS_NOICE]     = "NOICE",
    [ANS_DISTURBER] = "DISTURBER",
};

// indexes of weather values
enum{
    NINTERRUPT,
    NENERGY,
    NDISTANCE,
    NLIGHTNING,
    NSENSNO,
    NAMOUNT
};

static const val_t values[NAMOUNT] = {
    [NINTERRUPT]    = {.sense = VAL_RECOMMENDED, .type = VALT_UINT, .meaning = IS_OTHER, .name = "LINTR", .comment = "Lightning int.: 3 - lightning, 4 - noice, 5 - disturber"},
    [NENERGY]       = {.sense = VAL_RECOMMENDED, .type = VALT_UINT, .meaning = IS_OTHER, .name = "LENERGY", .comment = "Last lightning energy"},
    [NDISTANCE]     = {.sense = VAL_OBLIGATORY,  .type = VALT_UINT, .meaning = IS_OTHER, .name = "LIGTDIST", .comment = "Distance to last lightning, km"},
    [NLIGHTNING]    = {.sense = VAL_FORCEDSHTDN, .type = VALT_UINT, .meaning = IS_FORCEDSHTDN, .name = "LIGHTNIN", .comment = "Lightning event occured < 5km"},
    [NSENSNO]       = {.sense = VAL_RECOMMENDED, .type = VALT_UINT, .meaning = IS_OTHER, .name = "LSENSNO", .comment = "Last lightning event sensor number"},
};

/**
 * @brief parse_string - parsing of sensor's answer
 * @param str - text string with data
 * @param val - value of key
 * @return index of key in `values` or -1 if not found
 */
int parse_string(const char *str, uint32_t *val, uint32_t *nsens){
    if(!str) return -1;
    char key[SL_KEY_LEN], value[SL_VAL_LEN];
    DBG("String: %s", str);
    if(2 != sl_get_keyval(str, key, value)) return -1;
    DBG("key=%s, val=%s", key, value);
    int l = strlen(key);
    int SensNo = key[--l] - '0';
    DBG("SensNo=%d", SensNo);
    if(SensNo < 0 || SensNo > 1) return -1;
    key[l] = 0;
    int idx = 0;
    for(; idx < NLIGHTNING; ++idx){
        if(0 == strcmp(key, commands[idx])) break;
    }
    if(idx == NLIGHTNING) return -1;
    uint32_t u32;
    if(idx == 0){ // check interrupt source
        if(strstr(value, commands[ANS_LIGHTNING])) u32 = ANS_LIGHTNING;
        else if(strstr(value, commands[ANS_DISTURBER])) u32 = ANS_DISTURBER;
        else u32 = ANS_NOICE;
    }else u32 = (uint32_t)atoi(value);
    DBG("idx = %u, val=%u", idx, u32);
    if(val) *val = u32;
    if(nsens) *nsens = (uint32_t)SensNo;
    return idx;
}

static void *mainthread(void *s){
    FNAME();
    char buf[BUFSIZ];
    time_t tpoll = 0;
    sensordata_t *sensor = (sensordata_t *)s;
    while(sensor->fdes > -1){
        time_t tnow = time(NULL);
        if(tnow - tpoll > sensor->tpoll){
            int dlen = sprintf(buf, "%s0\n%s1\n", commands[CMD_DISTANCE], commands[CMD_DISTANCE]);
            if(dlen != write(sensor->fdes, buf, dlen)){
                WARN("Can't ask new data from lightning monitor");
                break;
            }
            DBG("poll @%zd, pollt=%zd", tnow, sensor->tpoll);
            tpoll = tnow;
        }
        int canread = sl_canread(sensor->fdes);
        if(canread < 0){
            WARNX("Disconnected fd %d", sensor->fdes);
            break;
        }else if(canread == 1){
            ssize_t got = read(sensor->fdes, buf, BUFSIZ);
            if(got > 0){
                sl_RB_write(sensor->ringbuffer, (uint8_t*)buf, got);
            }else if(got < 0){
                WARNX("Disconnected?");
                break;
            }
        }
        if(sl_RB_datalen(sensor->ringbuffer) > BUFSIZ-1){
            WARNX("Overfull? Clear data from ring buffer");
            sl_RB_clearbuf(sensor->ringbuffer);
        }
        int gotfresh = FALSE;
        pthread_mutex_lock(&sensor->valmutex);
        while(1){
            if(sl_RB_readline(sensor->ringbuffer, buf, BUFSIZ-1) > 0){
                tpoll = tnow;
                uint32_t val, nsens;
                int idx = parse_string(buf, &val, &nsens);
                if(idx > -1){
                    DBG("Got index=%d", idx);
                    gotfresh = TRUE;
                    if(idx == NINTERRUPT && val == ANS_LIGHTNING){
                        DBG("Interrupt: lightning");
                        sensor->values[NSENSNO].value.u = nsens;
                        sensor->values[NSENSNO].time = tnow;

                    }
                    sensor->values[idx].value.u = val;
                    sensor->values[idx].time = tnow;
                }
            }else break;
        }
        // now check values
        if(sensor->values[NINTERRUPT].time == tnow && sensor->values[NINTERRUPT].value.u == ANS_LIGHTNING){ // fresh strike
            if(tnow - sensor->values[NDISTANCE].time < 3 && sensor->values[NDISTANCE].value.u <= MINDIST){ // ahtung!
                if(sensor->values[NLIGHTNING].value.u == 0) DBG("Ahtung!");
                sensor->values[NLIGHTNING].time = tnow;
                sensor->values[NLIGHTNING].value.u = 1;
            }
        }else if(tnow - sensor->values[NINTERRUPT].time > TCHECK && sensor->values[NLIGHTNING].value.u){ // remove old lightning flag
            DBG("Clear ahtung");
            sensor->values[NLIGHTNING].value.u = 0;
            sensor->values[NLIGHTNING].time = tnow;
        }
        pthread_mutex_unlock(&sensor->valmutex);
        if(gotfresh) DBG("got fresh data");
        if(gotfresh && sensor->freshdatahandler){
            DBG("Run fresh data handler");
            sensor->freshdatahandler(sensor);
        }
        usleep(1000);
    }
    return NULL;
}

int sensor_init(sensordata_t *s){
    FNAME();
    if(!s) return FALSE;
    int fd = getFD(s->path);
    if(fd < 0) return FALSE;
    snprintf(s->name, NAME_LEN, "%s", SENSOR_NAME);
    s->fdes = fd;
    s->Nvalues = NAMOUNT;
    s->tpoll = TCHECK;
    s->values = MALLOC(val_t, NAMOUNT);
    for(int i = 0; i < NAMOUNT; ++i) s->values[i] = values[i];
    if(!(s->ringbuffer = sl_RB_new(BUFSIZ)) ||
        pthread_create(&s->thread, NULL, mainthread,  (void*)s)){
        return FALSE;
    }
    return TRUE;
}
