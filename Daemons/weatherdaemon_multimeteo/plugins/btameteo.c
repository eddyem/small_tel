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

enum{
    NWIND,
    NHUMIDITY,
    NAMB_TEMP,
    NPRESSURE,
    NPRECIP,
    NAMOUNT
};

extern sensordata_t sensor;

static const val_t values[NAMOUNT] = {
    [NWIND]     = {.sense = VAL_OBLIGATORY, .type = VALT_FLOAT, .meaning = IS_WIND},
    [NHUMIDITY] = {.sense = VAL_OBLIGATORY, .type = VALT_FLOAT, .meaning = IS_HUMIDITY},
    [NAMB_TEMP] = {.sense = VAL_OBLIGATORY, .type = VALT_FLOAT, .meaning = IS_AMB_TEMP},
    [NPRESSURE] = {.sense = VAL_OBLIGATORY, .type = VALT_FLOAT, .meaning = IS_PRESSURE},
    [NPRECIP]   = {.sense = VAL_OBLIGATORY, .type = VALT_UINT,  .meaning = IS_PRECIP},
};

static void *mainthread(void _U_ *U){
    FNAME();
    while(1){
        if(check_shm_block(&sdat)){
            DBG("Got next");
            time_t tnow = time(NULL);
            pthread_mutex_lock(&sensor.valmutex);
            for(int i = 0; i < NAMOUNT; ++i)
                sensor.values[i].time = tnow;
            sensor.values[NWIND].value.f = val_Wnd;
            sensor.values[NPRESSURE].value.f = val_B;
            sensor.values[NAMB_TEMP].value.f = val_T1;
            sensor.values[NHUMIDITY].value.f = val_Hmd;
            DBG("Tprecip=%.1f, tnow=%.1f", Precip_time, sl_dtime());
            sensor.values[NPRECIP].value.u = (tnow - (time_t)Precip_time < 60) ? 1 : 0;
            pthread_mutex_unlock(&sensor.valmutex);
            if(sensor.freshdatahandler) sensor.freshdatahandler(&sensor);
        }else break; // no connection?
        sleep(1);
    }
    DBG("Lost connection -> suicide");
    common_kill(&sensor);
    return NULL;
}

static int init(struct sensordata_t *s, int N, time_t pollt, int _U_ fd){
    FNAME();
    if(!s) return -1;
    sensor.PluginNo = N;
    if(pollt) s->tpoll = pollt;
    if(!get_shm_block(&sdat, ClientSide)){
        WARNX("Can't get BTA shared memory block");
        return -1;
    }
    if(pthread_create(&s->thread, NULL, mainthread, NULL)) return -1;
    s->values = MALLOC(val_t, NAMOUNT);
    for(int i = 0; i < NAMOUNT; ++i) s->values[i] = values[i];
    if(!(s->ringbuffer = sl_RB_new(BUFSIZ))){
        WARNX("Can't init ringbuffer!");
        common_kill(s);
        return -1;
    }
    return NAMOUNT;
}

sensordata_t sensor = {
    .name = "BTA 6-m telescope main meteostation",
    .Nvalues = NAMOUNT,
    .init = init,
    .onrefresh = common_onrefresh,
    .valmutex = PTHREAD_MUTEX_INITIALIZER,
    .get_value = common_getval,
    .kill = common_kill,
};
