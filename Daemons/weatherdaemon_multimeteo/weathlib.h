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

#pragma once

#include <stdint.h>
#include <time.h>

// length (in symbols) of key, value and comment
#define KEY_LEN         (8)
#define VAL_LEN         (31)
#define COMMENT_LEN     (63)
// maximal full length of "KEY=val / comment" (as for sfitsio)
#define FULL_LEN        (81)
// name of meteo-plugin
#define NAME_LEN        (127)

// importance of values
typedef enum{
    VAL_OBLIGATORY,     // can't be omitted
    VAL_RECOMMENDED,    // recommended to show
    VAL_UNNECESSARY,    // may be shown by user request
    VAL_BROKEN          // sensor is broken, omit it
} valsense_t;

// meaning of values
typedef enum{
    IS_WIND,            // wind, m/s
    IS_WINDDIR,         // wind direction, degr
    IS_HUMIDITY,        // humidity, percent
    IS_AMB_TEMP,        // ambient temperature, degC
    IS_INNER_TEMP,      // in-dome temperature, degC
    IS_HW_TEMP,         // hardware (?) termperature, degC
    IS_PRESSURE,        // atmospheric pressure, mmHg
    IS_PRECIP,          // precipitation (1 - yes, 0 - no)
    IS_PRECIP_LEVEL,    // precipitation level (mm)
    IS_MIST,            // mist (1 - yes, 0 - no)
    IS_CLOUDS,          // integral clouds value (bigger - better)
    IS_SKYTEMP,         // mean sky temperatyre
    IS_OTHER            // something other - read "name" and "comment"
} valmeaning_t;

typedef union{
    uint32_t u;
    int32_t i;
    float f;
} num_t;

// type of value
typedef enum{
    VALT_UINT,
    VALT_INT,
    VALT_FLOAT,
    //VALT_STRING,
} valtype_t;

// value
typedef struct{
    char name[KEY_LEN+1];   // max VAL_LEN symbols FITS header keyword name
    char comment[COMMENT_LEN+1];// max COMMENT_LEN symbols of comment to FITS header
    valsense_t sense;       // importance
    valtype_t type;         // type of given value
    valmeaning_t meaning;   // what type of sensor is it
    num_t value;            // value itself
    time_t time;            // last changing time
} val_t;

// all sensor's data
typedef struct sensordata_t{
    char name[NAME_LEN+1];  // max 31 symbol of sensor's name (e.g. "rain sensor")
    int Nvalues;            // amount of values
    int PluginNo;           // plugin number in array (if several)
    int (*init)(int, time_t, int); // init meteostation with given PluginNo, poll_interval and fd; return amount of parameters found
    int (*onrefresh)(void (*handler)(const struct sensordata_t* const)); // handler of new data; return TRUE if OK
    int (*get_value)(val_t *, int); // getter of Nth value
    void (*die)();          // close everything and die
} sensordata_t;
