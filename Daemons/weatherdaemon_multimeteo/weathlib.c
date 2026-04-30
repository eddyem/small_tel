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

// Some common functions and handlers for sensors

#include <pthread.h>

#include "weathlib.h"

// private functions (for plugins usage only)
//static int common_onrefresh(sensordata_t*, void (*handler)(sensordata_t*));
//static void common_kill(sensordata_t *);
//static int common_getval(sensordata_t*, val_t*, int);
//static int common_init(sensordata_t*, int, time_t, int);

/**
 * @brief common_new - call this function from your plugin's `sensor_new`
 * @return
 */
sensordata_t *common_new(){
    sensordata_t *s = MALLOC(sensordata_t, 1);
    s->fdes = -1; // not inited
    s->onrefresh = common_onrefresh;
    s->get_value = common_getval;
    s->kill = common_kill;
    pthread_mutex_init(&s->valmutex, NULL);
    return s;
}

/**
 * @brief sensor_alive - test if sensor's thread isn't dead
 * @param s - sensor
 * @return FALSE if thread is dead
 */
int sensor_alive(sensordata_t *s){
    if(!s || s->fdes < 0) return FALSE;
    if(pthread_kill(s->thread, 0)) return FALSE;
    return TRUE;
}

/**
 * @brief common_onrefresh - common `change onrefresh handler`
 * @param s - sensor
 * @return FALSE if failed
 */
int common_onrefresh(sensordata_t *s, void (*handler)(sensordata_t *)){
    FNAME();
    if(!s || !handler) return FALSE;
    s->freshdatahandler = handler;
    return TRUE;
}

/**
 * @brief common_kill - common `die` function
 * @param s - sensor
 */
void common_kill(sensordata_t *s){
    FNAME();
    if(!s) return;
    if(s->fdes > -1){ // inited and maybe have opened file/socket
        if(0 == pthread_cancel(s->thread)){
            DBG("%s main thread canceled, join", s->name);
            pthread_join(s->thread, NULL);
            DBG("Done");
        }
        close(s->fdes);
    }
    DBG("Delete RB");
    if(s->ringbuffer) sl_RB_delete(&s->ringbuffer);
    FREE(s->values);
    if(s->privdatafree) s->privdatafree(s->privdata);
    else FREE(s->privdata);
    DBG("Sensor '%s' killed", s->name);
    LOGERR("Sensor '%s' killed", s->name);
    FREE(s);
    DBG("There's no more this sensor");
}

/**
 * @brief common_getval - common value getter
 * @param s (i) - station
 * @param o (o) - value or NULL (if you just wants test N)
 * @param N - number of sensor
 * @return FALSE if failed
 */
int common_getval(struct sensordata_t *s, val_t *o, int N){
    if(!s || N < 0 || N >= s->Nvalues) return FALSE;
    if(o){
        pthread_mutex_lock(&s->valmutex);
        *o = s->values[N];
        pthread_mutex_unlock(&s->valmutex);
    }
    return TRUE;
}
