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

#include <fitsio.h>
#include <math.h>
#include <sofa.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>


int status = 0;
// check fits error status
int chkstatus(){
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
char *getFITSkeyval(fitsfile *fptr, const char *key){
    char card[FLEN_CARD], newcard[FLEN_CARD], comment[FLEN_COMMENT];
    static char value[FLEN_VALUE];
    int status = 0;
    int keytype;
    if(fits_read_card(fptr, key, card, &status) || !*card){
        fprintf(stderr, "Keyword %s does not exist or empty\n", key);
        return NULL;
    }
    fits_parse_value(card, value, comment, &status);
    if(chkstatus()) return NULL;
    return value;
}

// safely convert string to double (@return 0 if all OK)
int getdouble(double *d, const char *str){
    double res = -1.;
    char *endptr;
    if(!str) return 1;
    res = strtod(str, &endptr);
    if(endptr == str || *str == '\0'){ // || *endptr != '\0'){
        return 1;
    }
    if(d) *d = res;
    return 0;
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
char *d2s(double dbl, int ishours){
    int d, m;
    char *s = "";
    if(ishours) dbl /= 15.;
    if(dbl < 0.){
        s = "-";
        dbl = -dbl;
    }
    d = (int)dbl;
    dbl = (dbl-d)*60.;
    m = (int)dbl;
    dbl = (dbl-m)*60.;
    static char res[32];
    snprintf(res, 32, "%s%02d:%02d:%04.1f", s, d, m, dbl);
    return res;
}

typedef struct{
    double ra;
    double dec;
} polar;

/**
 * @brief J2000toJnow - convert ra/dec between epochs
 * @param in  - J2000 (degrees)
 * @param out - Jnow  (degrees)
 * @return
 */
int J2000toJnow(const polar *in, polar *out){
    if(!out) return 1;
    double utc1, utc2;
    time_t tsec;
    struct tm *ts;
    tsec = time(0); // number of seconds since the Epoch, 1970-01-01 00:00:00 +0000 (UTC)
    ts = gmtime(&tsec);
    int result = 0;
    result = iauDtf2d ( "UTC", ts->tm_year+1900, ts->tm_mon+1, ts->tm_mday, ts->tm_hour, ts->tm_min, ts->tm_sec, &utc1, &utc2 );
    if (result != 0) {
        fprintf(stderr, "iauDtf2d call failed\n");
        return 1;
    }
    // Make TT julian date for Atci13 call
    double tai1, tai2;
    double tt1, tt2;
    result = iauUtctai(utc1, utc2, &tai1, &tai2);
    if(result){
        fprintf(stderr, "iauUtctai call failed\n");
        return 1;
    }
    result = iauTaitt(tai1, tai2, &tt1, &tt2);
    if(result){
        fprintf(stderr, "iauTaitt call failed\n");
        return 1;
    }
    double pr = 0.0;     // RA proper motion (radians/year; Note 2)
    double pd = 0.0;     // Dec proper motion (radians/year)
    double px = 0.0;     // parallax (arcsec)
    double rv = 0.0;     // radial velocity (km/s, positive if receding)
    double rc = DD2R * in->ra, dc = DD2R * in->dec; // convert into radians
    double ri, di, eo;
    iauAtci13(rc, dc, pr, pd, px, rv, tt1, tt2, &ri, &di, &eo);
    out->ra  = iauAnp(ri - eo) * DR2D;
    out->dec = di * DR2D;
    return 0;
}

/**
 * @brief getDval - get double value of keyword
 * @param ret   - double value
 * @param fptr  - fits file
 * @param key   - keyword to search
 * @return 0 if found and converted
 */
int getDval(double *ret, fitsfile *fptr, const char *key){
    if(!ret || !key) return 2;
    char *val = getFITSkeyval(fptr, key);
    if(!val) return 1;
    if(getdouble(ret, val)){
        fprintf(stderr, "Wrong %s value\n", key);
        return 1;
    }
    return 0;
}

// run command xy2sky @fname and return stdout (up to 1024 bytes)
char *exe(char *fname){
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
            *ptr = 0;
        }
        wait(NULL);
    }
    return ret;
#undef die
}

int main(int argc, char **argv) {
    if(argc != 2){
        fprintf(stderr, "USAGE: %s fitsfile\n\tCalculate data :newalpt by given plate-recognized file\n", argv[0]);
        return 1;
    }
    double ra_center = 12., dec_center = 21., ra_scope, dec_scope;
    // get CENTER:
    char *val = exe(argv[1]);
    if(!val) return 1;
    char *p = strchr(val, ' ');
    if(!p) return 1;
    getdouble(&ra_center, val);
    getdouble(&dec_center, p);
    // get FITS keywords
    fitsfile *fptr;
    int iomode = READONLY;
    fits_open_file(&fptr, argv[1], iomode, &status);
    iomode = chkstatus();
    if(iomode) return iomode;
    if(getDval(&ra_scope, fptr, "RA")) return 4;
    if(getDval(&dec_scope, fptr, "DEC")) return 5;
    val = getFITSkeyval(fptr, "PIERSIDE");
    if(!val) return 6;
    char pierside = 'W';
    if(strstr(val, "East")) pierside = 'E';
    val = getFITSkeyval(fptr, "LSTEND");
    if(!val) return 7;
    char *s = strchr(val, '\'');
    if(!s) return 8;
    char *sidtm = strdup(++s);
    s = strchr(sidtm, '\'');
    if(!s) return 9;
    *s = 0;
    fits_close_file(fptr, &status);
    chkstatus();

    polar J2000 = {.ra = ra_center, .dec = dec_center}, Jnow;
    if(J2000toJnow(&J2000, &Jnow)) return 1;

    char    *sra_scope  = strdup(d2s(ra_scope, 1)),
            *sdec_scope = strdup(d2s(dec_scope, 0)),
            *sra_center = strdup(d2s(Jnow.ra, 1)),
            *sdec_center= strdup(d2s(Jnow.dec, 0));

    // create line like
    // :newalpt10:10:34.8,-12:21:14,E,10:10:3.6,-12:12:56,11:15:12.04#
    //         MRA          MDEC   MSIDE  PRA    PDEC      SIDTIME
    printf(":newalpt%s,%s,%c,%s,%s,%s#\n", sra_scope, sdec_scope, pierside, sra_center, sdec_center, sidtm);
    free(sra_scope); free(sdec_scope);
    free(sra_center); free(sdec_center);
    //free(sidtm);

    return(0);
}

