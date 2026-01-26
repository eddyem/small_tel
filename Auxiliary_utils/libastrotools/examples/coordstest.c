/*
 * This file is part of the ERFA project.
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
#include <time.h>
#include <usefull_macros.h>
#include "astrotools.h"

static at_string_t *S = NULL;

static const char *radtohrs(double r){
    if(!S) S = at_newstring(256);
    if(!at_radtoHtime(r, S)) ERRX("at_radtoHtime");
    return S->str;
}
static const char *radtodeg(double r){
    if(!S) S = at_newstring(256);
    if(!at_radtoHdeg(r, S)) ERRX("at_radtoHdeg");
    return S->str;
}


int main(){
    sl_init();
    at_MJD_t mjd;
    if(!at_get_MJDu(time(NULL), &mjd)) ERRX("at_get_MJDu");
    printf("MJD=%g; TAI=%g/%g, TT=%g/%g, UTC=%g/%g\n", mjd.MJD, mjd.tai1, mjd.tai2, mjd.tt1, mjd.tt2, mjd.utc1, mjd.utc2);
    double ST = at_get_LST(&mjd);
    if(ST < 0.) ERRX("at_get_LST");
    printf("ST = %s\n", radtohrs(ST));
    at_horiz_t htest = {.az = ERFA_DD2R*91., .zd = ERFA_DPI/4.}; // Z=45degr, A=1degr from south to west
    printf("hzd=%g\n", htest.zd);
    at_equat_t ptest;
    at_hor2eq(&htest, &ptest, ST);
    printf("A=%s, ", radtodeg(htest.az));
    printf("Z=%s; ", radtodeg(htest.zd));
    printf("HOR->EQ: HA=%s, ", radtohrs(ptest.ha));
    printf("RA=%s, ", radtohrs(ptest.ra));
    printf("DEC=%s\n", radtodeg(ptest.dec));
    at_horiz_t h2;
    at_eq2hor(&ptest, &h2);
    printf("Back conversion EQ->HOR: A=%s, ", radtodeg(h2.az));
    printf("Z=%s\n", radtodeg(h2.zd));
    at_equat_t pnow;
    if(!at_get_ObsPlace(&mjd, &ptest, &pnow, &h2)) ERRX("at_get_ObsPlace");
    printf("\nApparent place, RA=%s, ", radtohrs(pnow.ra-pnow.eo));
    printf("HA=%s, ", radtohrs(pnow.ha));
    printf("ST-RA=%s, ", radtohrs(ST-pnow.ra+pnow.eo));
    printf("DEC=%s; ", radtodeg(pnow.dec));
    printf("A=%s, ", radtodeg(h2.az));
    printf("Z=%s\n", radtodeg(h2.zd));
    at_hor2eq(&h2, &ptest, ST);
    printf("\tHOR->EQ: RA=%s, ", radtohrs(ptest.ra-ptest.eo));
    printf("HA=%s, ", radtohrs(ptest.ha));
    printf("ST-RA=%s, ", radtohrs(ST-ptest.ra+ptest.eo));
    printf("DEC=%s\n", radtodeg(ptest.dec));
    at_eq2hor(&pnow, &h2);
    printf("\tEQ->HOR: A=%s, ", radtodeg(h2.az));
    printf("Z=%s\n", radtodeg(h2.zd));
    if(!at_get_mean(&mjd, &pnow, &ptest)) ERRX("at_get_mean");
    printf("\nBack conversion pnow to mean place, ");
    printf("RA=%s, ", radtohrs(ptest.ra));
    printf("Dec=%s\n", radtodeg(ptest.dec));
    if(!at_obs2catP(&mjd, &pnow, &ptest)) ERRX("at_obs2catP");
    printf("And back to J2000 by observed pnow: ");
    printf("RA=%s, ", radtohrs(ptest.ra));
    printf("Dec=%s\n", radtodeg(ptest.dec));
    return 0;
}
