/*
 * Copyright 2020 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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

#include "cmdlnopts.h"
#include "sofatools.h"

#include <fitsio.h>
#include <libgen.h>
#include <math.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <usefull_macros.h>

static const double hpa2mm = 760. / 1013.25; // hPa->mmHg

static glob_pars *G = NULL;
static int status = 0;
// check fits error status
static int chkstatus(){
    int os = status;
    status = 0;
    if(os) fits_report_error(stderr, os);
    return os;
}

/**
 * @brief getFITSkeyval - find value of given keyword in FITS file
 * @param fptr    - fits file
 * @param val (o) - value (if key found)
 * @param key (i) - keyword
 * @return value (static char) or NULL
 */
static char *getFITSkeyval(fitsfile *fptr, const char *key){
    char card[FLEN_CARD], comment[FLEN_COMMENT];
    static char value[FLEN_VALUE];
    int status = 0;
    if(fits_read_card(fptr, key, card, &status) || !*card){
        fprintf(stderr, "Keyword %s does not exist or empty\n", key);
        return NULL;
    }
    fits_parse_value(card, value, comment, &status);
    if(chkstatus()) return NULL;
    return value;
}

// safely convert string to double (@return NULL if bad or return pointer to next non-digit)
static char *getdouble(double *d, const char *str){
    double res = -1.;
    char *endptr;
    if(!str || *str == 0) return NULL;
    res = strtod(str, &endptr);
    if(endptr == str) return NULL;
    if(d) *d = res;
    return endptr;
}
/*
// convert string like DD:MM:SS or HH:MM:SS into double, ishours==1 if HH
int hd2d(double *dbl, const char *str, int ishours){
    float s, sgn = 1.;
    int d, m;
    if(3 != sscanf(str, "%d:%d:%f", &d, &m, &s)){
        fprintf(stderr, "Wrong format: %s\n", str);
        return 1;
    }
    if(d < 0 || *str == '-'){ sgn = -1.; d = -d;}
    s = sgn*(d + (double)m/60. + s/3600.);
    if(ishours) s *= 15.; // convert hours to degrees
    if(dbl) *dbl = s;
    return 0;
}
*/
/**
 * @brief d2s     - convert angle to string
 * @param dbl     - angle value
 * @param ishours - ==0 if angle is in degrees
 * @return formed string
 */
static char *d2s(double dbl, int ishours){
    int d, m;
    char *s = "";
    if(ishours) dbl /= 15.;
    if(dbl < 0.){
        s = "-";
        dbl = -dbl;
    }else if(!ishours) s = "+";
    d = (int)dbl;
    dbl = (dbl-d)*60.;
    m = (int)dbl;
    dbl = (dbl-m)*60.;
    static char res[32];
    // no digits after decimal point in degrees
    if(ishours) snprintf(res, 32, "%s%02d:%02d:%04.1f", s, d, m, dbl);
    else snprintf(res, 32, "%s%02d:%02d:%02.0f", s, d, m, dbl);
    return res;
}

/**
 * @brief getDval - get double value of keyword
 * @param ret   - double value
 * @param fptr  - fits file
 * @param key   - keyword to search
 * @return 0 if found and converted
 */
static int getDval(double *ret, fitsfile *fptr, const char *key){
    if(!ret || !key) return 2;
    char *val = getFITSkeyval(fptr, key);
    if(!val) return 1;
    if(!getdouble(ret, val)){
        fprintf(stderr, "Wrong %s value\n", key);
        return 1;
    }
    return 0;
}

// run command xy2sky @fname and return stdout (up to 1024 bytes)
static char *exe(char *fname){
#define die(text)  do{fprintf(stderr, "%s\n", text); return NULL; }while(0)
    int link[2];
    pid_t pid;
    static char ret[1024] = {0};
    if(pipe(link)==-1) die("pipe");
    if((pid = fork()) == -1) die("fork");
    if(pid == 0){
        dup2(link[1], STDOUT_FILENO);
        close(link[0]);
        close(link[1]);
        execl("/usr/bin/xy2sky", "xy2sky", "-d", fname, "2076", "2064", (char *)0);
        die("execl");
    }else{
        close(link[1]);
        int nleave = 1023, r;
        char *ptr = ret;
        while(0 != (r = read(link[0], ptr, nleave))){
            ptr += r;
            nleave -= r;
            *ptr = 0;
        }
        wait(NULL);
    }
    return ret;
#undef die
}

static int parse_fits_file(char *name){
    double ra_center = 400., dec_center = 400., ra_scope, dec_scope;
    // get CENTER:
    char *val = exe(name);
    if(!val) return 1;
    DBG("EXE gives: %s", val);
//    char *p = strchr(val, ' ');
//    if(!p) return 1;
    val = getdouble(&ra_center, val);
    if(!val) return 1;
    if(!getdouble(&dec_center, val)) return 1;
    DBG("J2000=%g/%g", ra_center, dec_center);
    // get FITS keywords
    fitsfile *fptr;
    int iomode = READONLY;
    fits_open_file(&fptr, name, iomode, &status);
    iomode = chkstatus();
    if(iomode) return iomode;
    if(getDval(&ra_scope, fptr, "RA")) return 4;
    if(getDval(&dec_scope, fptr, "DEC")) return 5;
    double uxt = -1.;
    if(getDval(&uxt, fptr, "UNIXTIME")){ // no field "UNIXTIME" - search "JD"
        if(getDval(&uxt, fptr, "JD")){ // no "JD" - search MJD
            if(!getDval(&uxt, fptr, "MJD"))
                uxt = (uxt - 40587.) * 86400.; // convert MJD to Unix time
        }else uxt = (uxt - 2440587.5) * 86400.; // convert JD to Unix time
    }
    if(uxt < 0.) return 55;
    struct timeval tv;
    tv.tv_sec = (time_t) uxt;
    tv.tv_usec = 0;
    val = getFITSkeyval(fptr, "PIERSIDE");
    if(!val) return 6;
    char pierside = 'W';
    if(strstr(val, "East")) pierside = 'E';
    fits_close_file(fptr, &status);
    chkstatus();

    polarCrds J2000 = {.ra = ERFA_DD2R * ra_center, .dec = ERFA_DD2R * dec_center}, Jnow;
    DBG("J2000=%g/%g", ra_center, dec_center);
    DBG("J2000=%g/%g", J2000.ra/ERFA_DD2R, J2000.dec/ERFA_DD2R);
    if(get_ObsPlace(&tv, &J2000, &Jnow, NULL)) return 1;
    DBG("JNOW: RA=%g, DEC=%g, EO=%g", Jnow.ra/ERFA_DD2R, Jnow.dec/ERFA_DD2R, Jnow.eo/ERFA_DD2R);
    sMJD mjd;
    if(get_MJDt(&tv, &mjd)) return 1;
    double ST;
    almDut adut;
    if(getDUT(&adut)) return 1;
    placeData *place = getPlace();
    if(!place) return 1;
    if(get_LST(&mjd, adut.DUT1, place->slong, &ST)) return 1;

    double ra_now = (Jnow.ra - Jnow.eo)/ERFA_DD2R, dec_now = Jnow.dec/ERFA_DD2R;
    DBG("RA_now=%g, DEC_now=%g", ra_now, dec_now);

    if(G->horcoords){ // horizontal coordinates: change ra->AZ, dec->ZD
        horizCrds h_s, h_now;
        polarCrds p_s = {.ra = ERFA_DD2R * ra_scope, .dec = ERFA_DD2R * dec_scope};
        eq2hor(&p_s, &h_s, ST);
        eq2hor(&Jnow, &h_now, ST);
        ra_scope = h_s.az/ERFA_DD2R; dec_scope = h_s.zd/ERFA_DD2R;
        ra_now = h_now.az/ERFA_DD2R; dec_now = h_now.zd/ERFA_DD2R;
    }
    ST /= ERFA_DD2R; // convert radians to degrees
    if(G->ha && !G->horcoords){ // print HA instead of RA
        ra_scope = ST - ra_scope;
        if(ra_scope < 0.) ra_scope += ERFA_D2PI;
        ra_now = ST - ra_now;
        if(ra_now < 0.) ra_now += ERFA_D2PI;
    }
    if(G->delta){
        ra_now -= ra_scope;
        dec_now -= dec_scope;
    }
    int rainhrs = !G->raindeg;
    char *sidtm = NULL, *sra_scope  = NULL, *sdec_scope = NULL, *sra_center = NULL, *sdec_center= NULL;
    if(G->crdstrings){ // string form
        sidtm = strdup(d2s(ST, G->stindegr ? 0 : 1));
        sra_scope  = strdup(d2s(ra_scope, rainhrs));
        sdec_scope = strdup(d2s(dec_scope, 0));
        sra_center = strdup(d2s(ra_now, rainhrs));
        sdec_center= strdup(d2s(dec_now, 0));
    }

    // for 10-micron output create line like
    // :newalpt10:10:34.8,-12:21:14,E,10:10:3.6,-12:12:56,11:15:12.04#
    //         MRA          MDEC   MSIDE  PRA    PDEC      SIDTIME
    // MRA: HH.MM.SS.S - mount-reported RA
    // MDEC: sDD:MM:SS - mount-reported DEC
    // MSIDE: 'E'/'W'  - pier-side
    // PRA: HH:MM:SS.S - plate-solved RA
    // PDEC: sDD:MM:SS - plate-solved DEC
    // SIDTIME: HH:MM:SS.S - local sid.time
    if(G->for10m){
        printf(":newalpt%s,%s,%c,%s,%s,%s#\n", sra_scope, sdec_scope, pierside, sra_center, sdec_center, sidtm);
    }else{
        if(G->crdstrings){
            printf("%-16s%-16s   %c   %-18s%-19s", sra_scope, sdec_scope, pierside, sra_center, sdec_center);
            if(!G->ha) printf("%-15s", sidtm);
        }else{
            if(!G->raindeg){ ra_scope /= 15.; ra_now /= 15.; }
            printf("%-16.8f%-16.8f   %c   %-18.8f%-19.8f", ra_scope, dec_scope, pierside, ra_now,  dec_now);
            if(!G->ha) printf("%-15.8f", G->stindegr ? ST : ST/15.);
        }
        printf("%-15s\n", basename(name));
    }
    FREE(sra_scope); FREE(sdec_scope);
    FREE(sra_center); FREE(sdec_center);
    FREE(sidtm);
    return 0;
}

static void printheader(){
    printf("# Pointing data @ p=%.f %s, T=%.1f degrC", G->pressure*(G->pmm ? hpa2mm : 1.), G->pmm ? "mmHg" : "hPa", G->temperature);
    const char *raha = G->ha ? "HA" : "RA";
    const char *deczd = "DEC";
    if(G->horcoords){
        raha = "AZ";
        deczd = " ZD";
        printf(", AZ from north clockwise");
    }
    printf("\n");
    const char *raunits, *decunits;
    if(G->crdstrings){
        raunits = G->raindeg ? "dms" : "hms";
        decunits = "dms";
    }else{
        raunits = G->raindeg ? "deg" : "hrs";
        decunits = "deg";
    }
    const char *apparent = G->delta ? "(app-enc)" : "Apparent";
    char a[4][32];
    snprintf(a[0], 32, "Encoder %s,%s", raha, raunits);
    snprintf(a[1], 32, "Encoder %s,%s", deczd, decunits);
    snprintf(a[2], 32, "%s %s,%s", apparent, raha, raunits);
    snprintf(a[3], 32, "%s %s,%s", apparent, deczd, decunits);
    printf("%-16s%-16s Pier  %-18s%-19s", a[0], a[1], a[2], a[3]);
    if(!G->ha){
        printf("Sid. time,");
        if(G->crdstrings){
            if(G->stindegr) printf("dms");
            else printf("hms");
        }else{
            if(G->stindegr) printf("deg");
            else printf("hrs");
        }
        printf("  ");
    }
    printf("Filename\n");
}

int main(int argc, char **argv) {
    initial_setup();
    G = parse_args(argc, argv);
    if(G->pressure < 0.) ERRX("Pressure should be greater than zero");
    if(G->temperature < -100. || G->temperature > 100.) ERRX("Temperature over the range -100..+100");
    if(G->pmm) G->pressure /= hpa2mm;
    setWeath(G->pressure, G->temperature, 0.5);
    if(G->for10m){
        G->horcoords = 0;
        G->crdstrings = 1;
        G->raindeg = 0;
        G->ha = 0;
        G->stindegr = 0;
    }else if(G->horcoords){
        G->ha = 1; // omit Hour Angle output
        G->raindeg = 1; // both coordinates are in degrees
    }
    if(G->printhdr){
        printheader();
        if(G->nfiles < 1) return 0;
    }
    if(G->nfiles < 1){
        WARNX("Need at least one FITS filename");
        return 1;
    }
    for(int i = 0; i < G->nfiles; ++i)
        if(parse_fits_file(G->infiles[i])) WARNX("Can't parse file %s", G->infiles[i]);
    return 0;
}

