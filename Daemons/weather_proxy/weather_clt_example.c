/*
 * This file is part of the weather_proxy project.
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

#include "weather_data.h"
#include <stdio.h>

int main() {
    weather_data_t wd;
    if(get_weather_data(&wd) == 0){
        char strt[64];
        struct tm *T = localtime(&wd.last_update);
        strftime(strt, 63, "%F %T", T);
        printf("Prohibited: %d\nWeather: %d\nMax wind: %.1f\nWind: %.1f\nTemp: %.1f\nPressure: %.1f\nHumidity: %.1f\nupdated @%zd (%s)\n",
               wd.prohibited, wd.weather, wd.windmax, wd.wind, wd.exttemp, wd.pressure, wd.humidity,
               wd.last_update, strt);
    }else{
        fprintf(stderr, "Failed to get weather data\n");
    }
    return 0;
}
