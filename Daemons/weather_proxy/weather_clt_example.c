#include "weather_data.h"
#include <stdio.h>

int main() {
    weather_data_t wd;
    if (get_weather_data(&wd) == 0) {
        printf("Weather: %d, Max wind: %.1f, Wind: %.1f, Temp: %.1f; updated @%.1f\n",
               wd.weather, wd.windmax, wd.wind, wd.exttemp, wd.last_update);
    } else {
        fprintf(stderr, "Failed to get weather data\n");
    }
    return 0;
}
