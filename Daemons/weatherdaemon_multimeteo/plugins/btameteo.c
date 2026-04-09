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

#include "bta_shdata.h"
#include "weathlib.h"

#define SENSOR_NAME "BTA 6-m telescope main meteostation"

enum{
    NWIND,
    NHUMIDITY,
    NAMB_TEMP,
    NPRESSURE,
    NPRECIP,
    NAMOUNT
};

static const val_t values[NAMOUNT] = {
    [NWIND]     = {.sense = VAL_RECOMMENDED, .type = VALT_FLOAT, .meaning = IS_WIND},
    [NHUMIDITY] = {.sense = VAL_RECOMMENDED, .type = VALT_FLOAT, .meaning = IS_HUMIDITY},
    [NAMB_TEMP] = {.sense = VAL_RECOMMENDED, .type = VALT_FLOAT, .meaning = IS_AMB_TEMP},
    [NPRESSURE] = {.sense = VAL_RECOMMENDED, .type = VALT_FLOAT, .meaning = IS_PRESSURE},
    [NPRECIP]   = {.sense = VAL_RECOMMENDED, .type = VALT_UINT,  .meaning = IS_PRECIP},
};

static void *mainthread(void *s){
    FNAME();
    sensordata_t *sensor = (sensordata_t *)s;
    while(1){
        if(check_shm_block(&sdat)){
            DBG("Got next");
            time_t tnow = time(NULL);
            pthread_mutex_lock(&sensor->valmutex);
            for(int i = 0; i < NAMOUNT; ++i)
                sensor->values[i].time = tnow;
            sensor->values[NWIND].value.f = val_Wnd;
            sensor->values[NPRESSURE].value.f = val_B;
            sensor->values[NAMB_TEMP].value.f = val_T1;
            sensor->values[NHUMIDITY].value.f = val_Hmd;
            DBG("Tprecip=%.1f, tnow=%.1f", Precip_time, sl_dtime());
            sensor->values[NPRECIP].value.u = (tnow - (time_t)Precip_time < 60) ? 1 : 0;
            pthread_mutex_unlock(&sensor->valmutex);
            if(sensor->freshdatahandler) sensor->freshdatahandler(sensor);
        }else break; // no connection?
        sleep(1);
    }
    DBG("Lost connection -> suicide");
    sensor->kill(sensor);
    return NULL;
}

sensordata_t *sensor_new(int N, time_t pollt, int _U_ fd){
    FNAME();
    sensordata_t *s = common_new();
    if(!s) return NULL;
    s->PluginNo = N;
    if(pollt) s->tpoll = pollt;
    if(!get_shm_block(&sdat, ClientSide)){
        WARNX("Can't get BTA shared memory block or create main thread");
        s->kill(s);
        return NULL;
    }
    s->values = MALLOC(val_t, NAMOUNT);
    for(int i = 0; i < NAMOUNT; ++i) s->values[i] = values[i];
    s->Nvalues = NAMOUNT;
    strncpy(s->name, SENSOR_NAME, NAME_LEN);
    /*if(!(s->ringbuffer = sl_RB_new(BUFSIZ))){
        WARNX("Can't init ringbuffer!");
        common_kill(s);
        return -1;
    }*/
    if(pthread_create(&s->thread, NULL, mainthread, (void*)s)){
        WARN("Can't create main thread");
        s->kill(s);
        return NULL;
    }
    s->fdes = 0;
    return s;
}

