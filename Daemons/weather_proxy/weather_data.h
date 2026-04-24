#pragma once

#include <stdint.h>
#include <time.h>

#define SHM_NAME "/weather_shm"
#define SEM_NAME "/weather_sem"

typedef enum {
    WEATHER_GOOD = 0,           // may start observations
    WEATHER_BAD = 1,            // cannot start but can continue if want
    WEATHER_TERRIBLE = 2,       // close & park: wind, precipitation, humidity etc.
    WEATHER_PROHIBITED = 3,     // force closing & parking; power off equipment, ready to power off computer
} weather_condition_t;

typedef struct {
    weather_condition_t weather;    // conditions: field "WEATHER"
    float windmax;                  // maximal wind for last hour, m/s: "WINDMAX1"
    float wind;                     // current wind speed, m/s: "WIND"
    float clouds;                   // sky "quality" (>2500 - OK): "CLOUDS"
    float exttemp;                  // external temperature, degC: "EXTTEMP"
    float pressure;                 // atm. pressure, mmHg: "PRESSURE"
    float humidity;                 // humidity, percents: "HUMIDITY"
    int rain;                       // ==1 when rainy: "PRECIP"
    int prohibited;                 // ==1 if "weather == prohibited" or got `prohibited` signal -> ready to power off
    time_t last_update;             // value of "TMEAS"
} weather_data_t;

int get_weather_data(weather_data_t *data);

