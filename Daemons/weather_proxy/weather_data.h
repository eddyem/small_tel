#pragma once

#include <stdint.h>
#include <time.h>

typedef enum {
    WEATHER_GOOD = 0,
    WEATHER_BAD = 1,
    WEATHER_TERRIBLE = 2,
    WEATHER_PROHIBITED = 3,
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
    int prohibited;                 // ==1 if "weather == prohibited" or rain == 1
    time_t last_update;             // value of "TMEAS"
} weather_data_t;

int get_weather_data(weather_data_t *data);

