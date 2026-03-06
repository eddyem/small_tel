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

#include "weathlib.h"

/**
 * @brief sensor_alive - test if sensor's thread isn't dead
 * @param s - sensor
 * @return FALSE if thread is dead
 */
int sensor_alive(sensordata_t *s){
    if(!s) return FALSE;
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
    if(0 == pthread_kill(s->thread, -9)){
        DBG("%s main thread killed, join", s->name);
        pthread_join(s->thread, NULL);
        DBG("Done");
    }
    DBG("Delete RB");
    sl_RB_delete(&s->ringbuffer);
    if(s->fdes > -1){
        close(s->fdes);
        DBG("FD closed");
    }
    FREE(s->values);
    DBG("Sensor %s killed", s->name);
}
