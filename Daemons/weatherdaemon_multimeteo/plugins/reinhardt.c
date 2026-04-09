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

#include "weathlib.h"

#define SENSOR_NAME "Old Reinhard meteostation"

//static const char *emultemplate = "<?U> 06:50:36, 20.01.00, TE-2.20, DR1405.50, WU2057.68, RT0.00, WK1.00, WR177.80, WT-2.20, FE0.69, RE0.00, WG7.36, WV260.03, TI0.00, FI0.00,";

enum{
    NWIND,
    NWINDDIR,
    NHUMIDITY,
    NAMB_TEMP,
    NPRESSURE,
    NCLOUDS,
    NPRECIP,
    NPRECIPLVL,
    NAMOUNT
};

static const val_t values[NAMOUNT] = {
    [NWIND]     = {.sense = VAL_RECOMMENDED, .type = VALT_FLOAT, .meaning = IS_WIND},
    [NWINDDIR]  = {.sense = VAL_RECOMMENDED,.type = VALT_FLOAT, .meaning = IS_WINDDIR},
    [NHUMIDITY] = {.sense = VAL_BROKEN, .type = VALT_FLOAT, .meaning = IS_HUMIDITY},
    [NAMB_TEMP] = {.sense = VAL_RECOMMENDED, .type = VALT_FLOAT, .meaning = IS_AMB_TEMP},
    [NPRESSURE] = {.sense = VAL_BROKEN, .type = VALT_FLOAT, .meaning = IS_PRESSURE},
    [NCLOUDS]   = {.sense = VAL_OBLIGATORY, .type = VALT_FLOAT, .meaning = IS_CLOUDS},
    [NPRECIP]   = {.sense = VAL_RECOMMENDED, .type = VALT_UINT,  .meaning = IS_PRECIP},
    [NPRECIPLVL]= {.sense = VAL_UNNECESSARY,.type = VALT_FLOAT, .meaning = IS_PRECIP_LEVEL},
};

/**
 * @brief getpar - get parameter value
 * @param string (i) - string where to search
 * @param Val (o)    - value found
 * @param Name       - parameter name
 * @return TRUE if found
 */
static int getpar(char *string, double *Val, char *Name){
    if(!string || !Val || !Name) return FALSE;
    char *p = strstr(string, Name);
    if(!p) return FALSE;
    p += strlen(Name);
    //DBG("search %s", Name);
    char *endptr;
    *Val = strtod(p, &endptr);
    //DBG("eptr=%s, val=%g", endptr, *Val);
    if(endptr == string){
        WARNX("Double value not found");
        return FALSE;
    }
    return TRUE;
}

static void *mainthread(void *s){
    FNAME();
    char buf[BUFSIZ];
    time_t tpoll = 0;
    sensordata_t *sensor = (sensordata_t *)s;
    while(sensor->fdes > -1){
        time_t tnow = time(NULL);
        if(tnow - tpoll > sensor->tpoll){
            if(sl_tty_write(sensor->fdes, "?U\r\n", 4)){
                WARN("Can't ask new data");
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
                DBG("Disconnected?");
                break;
            }
        }
        if(sl_RB_datalen(sensor->ringbuffer) > BUFSIZ-1){
            WARNX("Overfull? Clear data from ring buffer");
            sl_RB_clearbuf(sensor->ringbuffer);
        }
        if(sl_RB_readto(sensor->ringbuffer, '\n', (uint8_t*)buf, BUFSIZ-1) > 0){
            tnow = time(NULL);
            DBG("Got next: %s", buf);
            pthread_mutex_lock(&sensor->valmutex);
            double d;
            //int Ngot = 0;
            if(getpar(buf, &d, "RE")){
                //++Ngot;
                sensor->values[NPRECIPLVL].value.f = (float) d;
                sensor->values[NPRECIPLVL].time = tnow;
                DBG("Got precip. lvl: %g", d);
            }
            if(getpar(buf, &d, "RT")){
                //++Ngot;
                sensor->values[NPRECIP].value.u = (d > 0.) ? 1 : 0;
                sensor->values[NPRECIP].time = tnow;
                DBG("Got precip.: %g", d);
            }
            if(getpar(buf, &d, "WU")){
                //++Ngot;
                sensor->values[NCLOUDS].value.f = (float) d;
                sensor->values[NCLOUDS].time = tnow;
                DBG("Got clouds.: %g", d);
            }
            if(getpar(buf, &d, "TE")){
                //++Ngot;
                sensor->values[NAMB_TEMP].value.f = (float) d;
                sensor->values[NAMB_TEMP].time = tnow;
                DBG("Got ext. T: %g", d);
            }
            if(getpar(buf, &d, "WG")){
                //++Ngot;
                d /= 3.6;
                DBG("Wind: %g", d);
                sensor->values[NWIND].value.f = (float) d;
                sensor->values[NWIND].time = tnow;
            }
            if(getpar(buf, &d, "WR")){
                //++Ngot;
                sensor->values[NWINDDIR].value.f = (float) d;
                sensor->values[NWINDDIR].time = tnow;
                DBG("Winddir: %g", d);
            }
            if(getpar(buf, &d, "DR")){
                //++Ngot;
                sensor->values[NPRESSURE].value.f = (float) (d * 0.7500616);
                sensor->values[NPRESSURE].time = tnow;
                DBG("Pressure: %g", d);
            }
            if(getpar(buf, &d, "FE")){
                //++Ngot;
                sensor->values[NHUMIDITY].value.f = (float) d;
                sensor->values[NHUMIDITY].time = tnow;
                DBG("Humidity: %g", d);
            }
            pthread_mutex_unlock(&sensor->valmutex);
            if(sensor->freshdatahandler) sensor->freshdatahandler(sensor);
        }
    }
    sensor->kill(sensor);
    return NULL;
}


sensordata_t *sensor_new(int N, time_t pollt, int fd){
    FNAME();
    if(fd < 0) return NULL;
    sensordata_t *s = common_new();
    if(!s) return NULL;
    s->Nvalues = NAMOUNT;
    s->PluginNo = N;
    s->fdes = fd;
    strncpy(s->name, SENSOR_NAME, NAME_LEN);
    if(pollt) s->tpoll = pollt;
    s->values = MALLOC(val_t, NAMOUNT);
    for(int i = 0; i < NAMOUNT; ++i) s->values[i] = values[i];
    if(!(s->ringbuffer = sl_RB_new(BUFSIZ)) ||
        pthread_create(&s->thread, NULL, mainthread, (void*)s)){
        s->kill(s);
        return NULL;
    }
    return s;
}
