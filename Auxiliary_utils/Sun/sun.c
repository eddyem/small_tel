/*
This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Library General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

Copyright (C) 2003 Liam Girdwood <liam@gnova.org>


A simple example showing some solar calculations.

*/

#include <ctype.h>
#include <errno.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libnova/solar.h>
#include <libnova/julian_day.h>
#include <libnova/rise_set.h>
#include <libnova/transform.h>

static const double horizons[] = {LN_SOLAR_STANDART_HORIZON, LN_SOLAR_CIVIL_HORIZON, LN_SOLAR_NAUTIC_HORIZON, LN_SOLAR_ASTRONOMICAL_HORIZON};
static const char *hnames[] = {"standard", "civil", "nautic", "astro"};

int main (int argc, char **argv){
    struct ln_equ_posn equ;
    struct ln_rst_time rst;
    struct ln_zonedate rise;
    struct ln_lnlat_posn observer;
    double JD, angle = LN_SOLAR_ASTRONOMICAL_HORIZON;
    time_t t;

    if(argc == 2){
        if(isdigit(*argv[1])) angle = -atof(argv[1]); // angle value
        else{ // angle name
            for(int i = 0; i < sizeof(horizons)/sizeof(double); ++i){
                if(strcasecmp(argv[1], hnames[i]) == 0){
                    angle = horizons[i];
                    break;
                }
            }
        }
    }else if(argc > 2){
        fprintf(stderr, "Usage: %s [positive angle in degr] OR %s [\"standard\"|\"civil\"|\"nautic\"|\"astro\"]\n",
                program_invocation_short_name, program_invocation_short_name);
        return 1;
    }

    observer.lat = 43.653528;
    observer.lng = 41.4414375;
    JD = ln_get_julian_from_sys();
    ln_get_solar_equ_coords (JD, &equ);
    if(ln_get_solar_rst_horizon(JD, &observer, angle, &rst) == 1) return 1;
    if(strcasecmp(program_invocation_short_name, "sunrise") == 0)
        ln_get_timet_from_julian(rst.rise, &t);
    else if(strcasecmp(program_invocation_short_name, "sunset") == 0)
        ln_get_timet_from_julian(rst.set, &t);
    else
        ln_get_timet_from_julian(rst.transit, &t);
    printf("%zd\n", t);
    return 0;
}
