#pragma once

#include <stdint.h>
#include <time.h>

typedef enum {
    WEATHER_GOOD = 0,
    WEATHER_BAD = 1, 
    WEATHER_TERRIBLE = 2 
} weather_condition_t;

typedef struct {
    weather_condition_t weather;
    float windmax;
    int rain;
    float clouds;
    float wind;
    float exttemp;
    float pressure;
    float humidity;
    int prohibited;
    double last_update;
} weather_data_t;

int get_weather_data(weather_data_t *data);

