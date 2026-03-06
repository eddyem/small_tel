/*
 * This file is part of the weatherdaemon project.
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

// dummy meteostation sending data each `tpoll` seconds

#include "weathlib.h"

#define NS (6)

extern sensordata_t sensor;

static const val_t values[NS] = { // fields `name` and `comment` have no sense until value meaning is `IS_OTHER`
    {.sense = VAL_OBLIGATORY, .type = VALT_FLOAT, .meaning = IS_WIND},
    {.sense = VAL_RECOMMENDED, .type = VALT_FLOAT, .meaning = IS_WINDDIR},
    {.sense = VAL_OBLIGATORY, .type = VALT_FLOAT, .meaning = IS_AMB_TEMP},
    {.sense = VAL_RECOMMENDED, .type = VALT_FLOAT, .meaning = IS_PRESSURE},
    {.sense = VAL_RECOMMENDED, .type = VALT_FLOAT, .meaning = IS_HUMIDITY},
    {.sense = VAL_OBLIGATORY, .type = VALT_UINT, .meaning = IS_PRECIP},
};

static void *mainthread(void _U_ *U){
    FNAME();
    double t0 = sl_dtime();
    while(1){
        float f = sensor.values[0].value.f + (drand48() - 0.5) / 2.;
        if(f >= 0.) sensor.values[0].value.f = f;
        f = sensor.values[1].value.f + (drand48() - 0.5) * 4.;
        if(f > 160. && f < 200.) sensor.values[1].value.f = f;
        f = sensor.values[2].value.f + (drand48() - 0.5) / 20.;
        if(f > 13. && f < 21.) sensor.values[2].value.f = f;
        f = sensor.values[3].value.f + (drand48() - 0.5) / 100.;
        if(f > 585. && f < 615.) sensor.values[3].value.f = f;
        f = sensor.values[4].value.f + (drand48() - 0.5) / 10.;
        if(f > 60. && f <= 100.) sensor.values[4].value.f = f;
        sensor.values[5].value.u = (f > 98.) ? 1 : 0;
        time_t cur = time(NULL);
        for(int i = 0; i < NS; ++i) sensor.values[i].time = cur;
        if(sensor.freshdatahandler) sensor.freshdatahandler(&sensor);
        while(sl_dtime() - t0 < sensor.tpoll) usleep(500);
        t0 = sl_dtime();
    }
    return NULL;
}

static int init(struct sensordata_t* s, int N, time_t pollt, int _U_ fd){
    FNAME();
    if(pthread_create(&s->thread, NULL, mainthread, NULL)) return 0;
    if(pollt) s->tpoll = pollt;
    s->values = MALLOC(val_t, NS);
    for(int i = 0; i < NS; ++i) s->values[i] = values[i];
    sensor.values[0].value.f = 1.;
    sensor.values[1].value.f = 180.;
    sensor.values[2].value.f = 17.;
    sensor.values[3].value.f = 600.;
    sensor.values[4].value.f = 80.;
    sensor.values[5].value.u = 0;
    sensor.PluginNo = N;
    return NS;
}

/**
 * @brief getval - value's getter
 * @param o (o) - value
 * @param N - it's index
 * @return FALSE if failed
 */
static int getval(struct sensordata_t* s, val_t *o, int N){
    if(N < 0 || N >= NS) return FALSE;
    if(o) *o = s->values[N];
    return TRUE;
}

sensordata_t sensor = {
    .name = "Dummy weatherstation",
    .Nvalues = NS,
    .init = init,
    .onrefresh = common_onrefresh,
    .get_value = getval,
    .kill = common_kill,
};
