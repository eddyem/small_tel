/*
 * This file is part of the astrotools project.
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

#include <stdio.h>
#include <usefull_macros.h>
#include "astrotools.h"

// convert coordinates from given epoch to J2000

typedef struct{
    int help;
    int obsplace;
    double ra;
    double dec;
    double JD;
    double unixtime;
    double longitude;
    double latitude;
    double altitude;
    double relhum;
    double phpa;
    double tdegc;
    double DUT1;
    double px;
    double py;
} parameters;

static parameters G = {
    .ra = -100.,
    .dec = -100.,
    .JD = -1.,
    .unixtime = -1.,
    .longitude = -1000.,
    .latitude = -1000.,
    .altitude = -1000.,
    .relhum = -1.,
    .phpa = -1.,
    .tdegc = -300.,
    .DUT1 = -100.,
    .px = -10000.,
    .py = -10000.
};

static sl_option_t cmdlnopts[] = {
    {"help",        NO_ARGS,    NULL,   'h',    arg_int,    APTR(&G.help),      "show this help"},
    {"obsplace",    NO_ARGS,    NULL,   'O',    arg_int,    APTR(&G.obsplace),  "input RA/Dec is observed place"},
    {"JD",          NEED_ARG,   NULL,   'J',    arg_double, APTR(&G.JD),        "Julian date"},
    {"unixtime",    NEED_ARG,   NULL,   'u',    arg_double, APTR(&G.unixtime),  "UNIX-time (seconds)"},
    {"ra",          NEED_ARG,   NULL,   'R',    arg_double, APTR(&G.ra),        "Right Ascention for given date (degrees)"},
    {"dec",         NEED_ARG,   NULL,   'D',    arg_double, APTR(&G.dec),       "Declination for given date (degrees)"},
    {"longitude",   NEED_ARG,   NULL,   'o',    arg_double, APTR(&G.longitude), "site longitude (degr)"},
    {"latitude",    NEED_ARG,   NULL,   'l',    arg_double, APTR(&G.latitude),  "site latitude (degr)"},
    {"altitude",    NEED_ARG,   NULL,   'a',    arg_double, APTR(&G.altitude),  "site altitude (meters)"},
    {"relhum",      NEED_ARG,   NULL,   'H',    arg_double, APTR(&G.relhum),    "relative humidity (0..1)"},
    {"phpa",        NEED_ARG,   NULL,   'P',    arg_double, APTR(&G.phpa),      "atm. pressure (hPa)"},
    {"tdegc",       NEED_ARG,   NULL,   'T',    arg_double, APTR(&G.tdegc),     "ambient temperature (degC)"},
    {"DUT1",        NEED_ARG,   NULL,   'd',    arg_double, APTR(&G.DUT1),      "DUT1 (seconds)"},
    {"px",          NEED_ARG,   NULL,   'x',    arg_double, APTR(&G.px),        "polar X (m)"},
    {"py",          NEED_ARG,   NULL,   'y',    arg_double, APTR(&G.py),        "polar Y (m)"},
    end_option
};


int main(int argc, char **argv){
    sl_init();
    sl_parseargs(&argc, &argv, cmdlnopts);
    if(G.help) sl_showhelp(-1, cmdlnopts);
    at_MJD_t MJD;
    G.ra *= ERFA_DD2R;
    G.dec *= ERFA_DD2R;
    if(G.ra < 0. || G.ra > ERFA_D2PI || G.dec < -ERFA_DPI/2. || G.dec > ERFA_DPI/2.)
        ERRX("Need RA (0..360 degr) and Dec (-90..90 degr)");
    if(G.JD < 0. && G.unixtime < 0.)
        ERRX("Need JD or unixtime");
    at_place_t place;
    at_getPlace(&place);
    at_weather_t weather;
    at_getWeath(&weather);
    at_dut_t dut;
    at_getDUT(&dut);
    G.longitude *= ERFA_DD2R;
    G.latitude *= ERFA_DD2R;
    if(G.longitude >= -ERFA_DPI && G.longitude <= ERFA_DPI) place.longitude = G.longitude;
    if(G.latitude >= -ERFA_DPI/2 && G.latitude <= ERFA_DPI/2) place.latitude = G.latitude;
    if(G.altitude > -100. && G.altitude < 12000.) place.altitude = G.altitude;
    if(G.relhum >= 0. && G.relhum <= 1.) weather.relhum = G.relhum;
    if(G.phpa >= 0. && G.phpa <= 1300.) weather.phpa = G.phpa;
    if(G.tdegc > -273.15 && G.tdegc < 100.) weather.tdegc = G.tdegc;
    if(G.DUT1 > -1. && G.DUT1 < 1.) dut.DUT1 = G.DUT1;
    if(G.px > -1000. && G.px < 1000.) dut.px = G.px;
    if(G.py > -1000. && G.py < 1000.) dut.py = G.py;
    at_setPlace(&place);
    DBG("Place: long=%g, lat=%g, alt=%g",
        place.longitude*ERFA_DR2D, place.latitude*ERFA_DR2D, place.altitude);
    at_setWeath(&weather);
    DBG("Weather: P=%g hPa, rho=%g%%, T=%g degrC",
        weather.phpa, weather.relhum*100., weather.tdegc);
    at_setDUT(&dut);
    DBG("DUT1=%g, px=%g, py=%g", dut.DUT1, dut.px, dut.py);
    if(G.JD > 0. && !at_get_MJDj(G.JD, &MJD)) ERRX("Bad julian date");
    if(G.unixtime > 0. && !at_get_MJDu(G.unixtime, &MJD)) ERRX("Bad UNIX time");
    DBG("Julian: MJD=%g, TT=%.2f+%g", MJD.MJD, MJD.tt1, MJD.tt2);
    at_equat_t p2000, pnow = {.ra = G.ra, .dec = G.dec, .eo = 0.};
    at_getHA(&pnow, at_get_LST(&MJD));
    DBG("in: ra=%.10f, dec=%.10f, ha=%.10f", pnow.ra*ERFA_DR2D, pnow.dec*ERFA_DR2D, pnow.ha*ERFA_DR2D);
    if(G.obsplace){ // ra/dec is observed place
        if(!at_obs2catP(&MJD, &pnow, &p2000)) ERRX("at_obs2catP");
        DBG("Observed");
    }else{ // ra/dec is catalog for given epoch
        if(!at_get_mean(&MJD, &pnow, &p2000)) ERRX("at_get_mean");
        DBG("Catalog");
    }
    at_string_t *s = at_newstring(128);
    at_radtoHtime(p2000.ra, s);
    printf("RA(h:m:s)=%s, ", s->str);
    at_radtoHdeg(p2000.dec, s);
    printf("Dec(d:m:s)=%s\n", s->str);
    printf("RA(degr)=%g, Dec(degr)=%g\n", p2000.ra*ERFA_DR2D, p2000.dec*ERFA_DR2D);
    return 0;
}
