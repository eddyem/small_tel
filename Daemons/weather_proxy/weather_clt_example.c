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
    if (get_weather_data(&wd) == 0) {
        printf("Weather: %d, Max wind: %.1f, Wind: %.1f, Temp: %.1f; updated @%zd\n",
               wd.weather, wd.windmax, wd.wind, wd.exttemp, wd.last_update);
    } else {
        fprintf(stderr, "Failed to get weather data\n");
    }
    return 0;
}
