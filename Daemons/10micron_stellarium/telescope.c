/*
 *                                                                                                  geany_encoding=koi8-r
 * telescope.c
 *
 * Copyright 2018 Edward V. Emelianov <eddy@sao.ru, edward.emelianoff@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 */
#include <arpa/inet.h>  // ntoa
#include <netinet/in.h> // ntoa
#include <pthread.h>
#include <sys/socket.h> // getpeername

#include "libsofa.h"
#include "main.h" // global_quit
#include "telescope.h"
#include "usefull_macro.h"

// polling timeout for answer from mount
#ifndef T_POLLING_TMOUT
#define T_POLLING_TMOUT (0.5)
#endif
// wait for '\n' after last data read
#ifndef WAIT_TMOUT
#define WAIT_TMOUT (0.01)
#endif

#define BUFLEN 80

static char *hdname = NULL;
static double ptRAdeg, ptDECdeg; // target RA/DEC J2000
static int Target = 0; // target coordinates entered

static double r = 0., d = 0.; // RA/DEC from wrhdr
static int mountstatus = 0; // return of :Gstat#
static time_t tlast = 0; // last time coordinates were refreshed

static int pause_communication = 0; // ==1 to prevent writing to port outside of terminal thread

/**
 * read strings from terminal (ending with '\n') with timeout
 * @return NULL if nothing was read or pointer to static buffer
 * THREAD UNSAFE!
 */
static char *read_string(){
    static char buf[BUFLEN];
    size_t r = 0, l;
    int LL = BUFLEN - 1;
    char *ptr = NULL;
    static char *optr = NULL;
    if(optr && *optr){
        ptr = optr;
        optr = strchr(optr, '\n');
        if(optr) ++optr;
        return ptr;
    }
    ptr = buf;
    double d0 = dtime();
    do{
        if((l = read_tty(ptr, LL))){
            r += l; LL -= l; ptr += l;
            if(ptr[-1] == '\n') break;
            d0 = dtime();
        }
    }while(dtime() - d0 < WAIT_TMOUT && LL);
    if(r){
        buf[r] = 0;
        optr = strchr(buf, '\n');
        if(optr) ++optr;
        return buf;
    }
    return NULL;
}

/**
 * write command, thread-safe
 * @param cmd  (i) - command to write
 * @param buff (o) - buffer (WHICH SIZE = BUFLEN!!!) to which write data (or NULL if don't need)
 * @return answer or NULL if error occured (or no answer)
 * WARNING!!! data returned is allocated by strdup! You MUST free it when don't need
 */
static char *write_cmd(const char *cmd, char *buff){
    static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&mutex);
    //DBG("Write %s", cmd);
    if(write_tty(cmd, strlen(cmd))) return NULL;
    double t0 = dtime();
    char *ans;
    while(dtime() - t0 < T_POLLING_TMOUT){ // read answer
        if((ans = read_string())){ // parse new data
            //DBG("got answer: %s", ans);
            pthread_mutex_unlock(&mutex);
            if(!buff) return NULL;
            strncpy(buff, ans, BUFLEN-1);
            return buff;
        }
    }
    pthread_mutex_unlock(&mutex);
    return NULL;
}

// write to telescope mount corrections: datetime, pressure and temperature
// @return 1 if time and weather was corrected
static int makecorr(){
    if(pause_communication) return 0;
    int ret = 1;
    // write current date&time
    char buf[64], ibuff[BUFLEN], *ans;
    DBG("curtime: %s", write_cmd(":GUDT#", ibuff));
    ans = write_cmd(":Gstat#", ibuff);
    if(ans){
        mountstatus = atoi(ans);
        // if system is in tracking or unknown state - don't update data!
        if(mountstatus == TEL_SLEWING || mountstatus == TEL_TRACKING) return 0;
    }
    /*
     * there's no GPS on this mount and there's no need for it!
    write_cmd(":gT#", NULL); // correct time by GPS
    ans = write_cmd(":gtg#", ibuff);
    */
    WARNX("Refresh datetime");
    time_t t = time(NULL);
    struct tm *stm = localtime(&t);
    struct timeval tv;
    gettimeofday(&tv,NULL);
    snprintf(buf, 64, ":SLDT%04d-%02d-%02d,%02d:%02d:%02d.%02ld#", 1900+stm->tm_year, stm->tm_mon+1, stm->tm_mday,
             stm->tm_hour, stm->tm_min, stm->tm_sec, tv.tv_usec/10000);
    DBG("write: %s", buf);
    ans = write_cmd(buf, ibuff);
    if(!ans || *ans != '1'){
        WARNX("Can't write current date/time");
        putlog("Can't set system time");
        ret = 0;
    }else{
        putlog("Set system time by command %s", buf);
    }
    DBG("curtime: %s", write_cmd(":GUDT#", ibuff));
    localWeather *w = getWeath();
    if(!w){
        ret = 0;
        putlog("Can't determine weather data");
    }else{ // set refraction model data
        snprintf(buf, 64, ":SRPRS%.1f#", w->pres*1013./760.);
        ans = write_cmd(buf, ibuff);
        if(!ans || *ans != '1'){
            ret = 0;
            putlog("Can't set pressure data of refraction model");
        }else putlog("Correct pressure to %gmmHg", w->pres);
        snprintf(buf, 64, ":SRTMP%.1f#", w->tc);
        ans = write_cmd(buf, ibuff);
        if(!ans || *ans != '1'){
            ret = 0;
            putlog("Can't set temperature data of refraction model");
        }else putlog("Correct temperature to %g", w->tc);
    }
    sprintf(buf, ":SREF1#"); // turn on refraction correction
    write_cmd(buf, ibuff);
    sprintf(buf, ":Sdat1#"); // turn on dual-axis tracking
    write_cmd(buf, ibuff);
    return ret;
}


int chkconn(){
    char tmpbuf[4096];
    read_tty(tmpbuf, 4096); // clear rbuf
    write_cmd("#", NULL); // clear cmd buffer
    if(!write_cmd(":SB0#", tmpbuf)) return 0; // 115200
    //if(!write_cmd(":GR#")) return 0;
    return 1;
}

/**
 * connect telescope device
 * @param dev (i)     - device name to connect
 * @param hdrname (i) - output file with FITS-headers
 * @return 1 if all OK
 */
int connect_telescope(char *dev, char *hdrname){
    if(!dev) return 0;
    tcflag_t spds[] = {B9600, B115200, B57600, B38400, B19200, B4800, B2400, B1200, 0}, *speeds = spds;
    DBG("Connection to device %s...", dev);
    while(*speeds){
        DBG("Try %d", *speeds);
        tty_init(dev, *speeds);
        if(chkconn()) break;
        ++speeds;
    }
    if(!*speeds) return 0;
    if(*speeds != B115200){
        restore_tty();
        tty_init(dev, B115200);
        if(!chkconn()) return 0;
    }
    write_cmd("#", NULL); // clear previous buffer
    write_cmd(":STOP#", NULL); // stop tracking after poweron
    write_cmd(":U2#", NULL); // set high precision
    write_cmd(":So10#", NULL); // set minimum altitude to 10 degrees
    putlog("Connected to %s@115200, will write FITS-header into %s", dev, hdrname);
    FREE(hdname);
    hdname = strdup(hdrname);
    DBG("connected");
    Target = 0;
    getWeath(); getPlace(); getDUT(); // determine starting values
    //write_cmd(":gT#", NULL); // correct time by GPS
    return 1;
}

/*
:MS# - move to target, return: 0 if all OK or text with error
:SrHH:MM:SS.SS# - set target RA (return 1 if all OK)
:SdsDD*MM:SS.S# - set target DECL (return 1 if all OK)
*/
/**
 * send coordinates to telescope
 * @param ra - right ascention (hours), Jnow without refraction
 * @param dec - declination (degrees), Jnow without refraction
 * @return 1 if all OK
 */
int point_telescope(double ra, double dec){
    if(pause_communication){
        putlog("Can't point telescope in paused mode");
        return 0;
    }
    DBG("try to send ra=%g, decl=%g", ra, dec);
    ptRAdeg = ra * 15.;
    ptDECdeg = dec;
    Target = 0;
    int err = 0;
    char buf[80], ibuff[BUFLEN];
    char sign = '+';
    if(dec < 0){
        sign = '-';
        dec = -dec;
    }

    int h = (int)ra;
    ra -= h; ra *= 60.;
    int m = (int)ra;
    ra -= m; ra *= 60.;

    int d = (int) dec;
    dec -= d; dec *= 60.;
    int dm = (int)dec;
    dec -= dm; dec *= 60.;
    snprintf(buf, 80, ":Sr%d:%d:%.2f#", h,m,ra);
    char *ans = write_cmd(buf, ibuff);
    if(!ans || *ans != '1'){
        err = 1;
        goto ret;
    }
    snprintf(buf, 80, ":Sd%c%d:%d:%.1f#", sign,d,dm,dec);
    ans = write_cmd(buf, ibuff);
    if(!ans || *ans != '1'){
        err = 2;
        goto ret;
    }
    DBG("Move");
    ans = write_cmd(":MS#", ibuff);
    if(!ans || *ans != '0'){
        putlog("move error, answer: %s", ans);
        err = 3;
        goto ret;
    }
    ret:
    if(err){
        putlog("error sending coordinates (err = %d: RA/DEC/MOVE)!", err);
        return 0;
    }else{
        Target = 1;
        putlog("Send ra=%g degr, dec=%g degr", ptRAdeg, ptDECdeg);
    }
    return 1;
}

/**
 * convert str into RA/DEC coordinate
 * @param str (i) - string with angle
 * @param val (o) - output angle value
 * @return 1 if all OK
 */
static int str2coord(char *str, double *val){
    if(!str || !val) return 0;
    int d, m;
    float s;
    int sign = 1;
    if(*str == '+') ++str;
    else if(*str == '-'){
        sign = -1;
        ++str;
    }
    int n = sscanf(str, "%d:%d:%f#", &d, &m, &s);
    if(n != 3) return 0;
    double ang = d + ((double)m)/60. + s/3600.;
    if(sign == -1) *val = -ang;
    else *val = ang;
    return 1;
}

/**
 * @brief printhdr - write FITS record into output file
 * @param fd   - fd to write
 * @param key  - key
 * @param val  - value
 * @param cmnt - comment
 * @return 0 if all OK
 */
static int printhdr(int fd, const char *key, const char *val, const char *cmnt){
    char tmp[81];
    char tk[9];
    if(strlen(key) > 8){
        snprintf(tk, 9, "%s", key);
        key = tk;
    }
    if(cmnt){
        snprintf(tmp, 81, "%-8s= %-21s / %s", key,  val, cmnt);
    }else{
        snprintf(tmp, 81, "%-8s= %s", key, val);
    }
    size_t l = strlen(tmp);
    tmp[l] = '\n';
    ++l;
    if(write(fd, tmp, l) != (ssize_t)l){
        WARN("write()");
        return 1;
    }
    return 0;
}

/**
 * get coordinates
 * @param ra (o)   - right ascension (hours)
 * @param decl (o) - declination (degrees)
 * @return telescope status or -1 if coordinates are too old
 */
int get_telescope_coords(double *ra, double *decl){
    if(!tlast) tlast = time(NULL);
    if(time(NULL) - tlast > COORDS_TOO_OLD_TIME) return -1; // coordinates are too old
    if(ra) *ra = r;
    if(decl) *decl = d;
    return mountstatus;
}

void stop_telescope(){ // work even in paused mode if moving!
    Target = 0;
    if(pause_communication){
        if(mountstatus == TEL_PARKED || mountstatus == TEL_STOPPED || mountstatus == TEL_INHIBITED
                || mountstatus ==  TEL_OUTLIMIT) return;
    }
    write_cmd(":RT9#", NULL);  // stop tracking
    write_cmd(":AL#", NULL);   // stop tracking
    write_cmd(":STOP#", NULL); // halt moving
}

// site characteristics
static char
    *elevation = NULL,
    *longitude = NULL,
    *latitude  = NULL;

// make duplicate of buf without trailing `#`
// if astr == 1, surround content with ''
static char *dups(const char *buf, int astr){
    if(!buf) return NULL;
    char *newbuf = malloc(strlen(buf)+5), *bptr = newbuf+1;
    if(!newbuf) return NULL;
    strcpy(bptr, buf);
    char *sharp = strrchr(bptr, '#');
    if(sharp) *sharp = 0;
    if(astr){
        bptr = newbuf;
        *bptr = '\'';
        int l = strlen(newbuf);
        newbuf[l] = '\'';
        newbuf[l+1] = 0;
    }
    char *d = strdup(bptr);
    free(newbuf);
    return d;
}

static void getplace(){
    char *ans, ibuff[BUFLEN];
    if(!elevation){
        ans = write_cmd(":Gev#", ibuff);
        elevation = dups(ans, 0);
    }
    if(!longitude){
        ans = write_cmd(":Gg#", ibuff);
        longitude = dups(ans, 1);
    }
    if(!latitude){
        ans = write_cmd(":Gt#", ibuff);
        latitude = dups(ans, 1);
    }
}

static const char *statuses[12] = {
    [TEL_TRACKING] = "'Tracking'",
    [TEL_STOPHOM] = "'Stopped or homing'",
    [TEL_PARKING] = "'Slewing to park'",
    [TEL_UNPARKING] = "'Unparking'",
    [TEL_HOMING] = "'Slewing to home'",
    [TEL_PARKED] = "'Parked'",
    [TEL_SLEWING] = "'Slewing or going to stop'",
    [TEL_STOPPED] = "'Stopped'",
    [TEL_INHIBITED] = "'Motors inhibited, T too low'",
    [TEL_OUTLIMIT] = "'Outside tracking limit'",
    [TEL_FOLSAT]= "'Following satellite'",
    [TEL_DATINCOSIST]= "'Data inconsistency'"
};

/**
 * @brief strstatus - return string explanation of mount status
 * @param status - integer status code
 * @return statically allocated string with explanation
 */
static const char* strstatus(int status){
    if(status < 0) return "'Signal lost'";
    if(status < TEL_MAXSTATUS) return statuses[status];
    if(status == 99) return "'Error'";
    return "'Unknown status'";
}

/**
 * @brief wrhdr - try to write into header file
 */
void wrhdr(){
    static time_t commWasPaused = 0;
    if(pause_communication){ // don't allow pauses more for 15 minutes!
        if(commWasPaused == 0){
            commWasPaused = time(NULL);
            return;
        }else{
            if(time(NULL) - commWasPaused > 15*60){
                putlog("Clear communication pause after 15 minutes");
                pause_communication = 0;
            }else return;
        }
    }
    static int failcounter = 0;
    static time_t lastcorr = 0; // last time of corrections made
    if(time(NULL) - lastcorr > CORRECTIONS_TIMEDIFF){ // make correction once per hour
        if(makecorr()) lastcorr = time(NULL);
        else lastcorr += 30; // failed -> check 30s later
    }
    char *ans = NULL, *jd = NULL, *lst = NULL, *date = NULL, *pS = NULL;
    char ibuff[BUFLEN];
    // get coordinates for writing to file & sending to stellarium client
    ans = write_cmd(":GR#", ibuff);
    if(!str2coord(ans, &r)){
        if(++failcounter == 10){
            putlog("Lost connection with mount");
            DBG("Can't get RA!");
            signals(9);
        }
        DBG("Failed");
        return;
    }
    ans = write_cmd(":GD#", ibuff);
    if(!str2coord(ans, &d)){
        if(++failcounter == 10){
            putlog("Lost connection with mount");
            DBG("Can't get DEC!");
            signals(9);
        }
        DBG("Failed");
        return;
    }
    almDut *dut = getDUT();
    localWeather *weather = getWeath();
    double LST = 0; // local sidereal time IN RADIANS!

    placeData *place = getPlace();
    if(get_LST(NULL, dut->DUT1, place->slong, &LST)){
        DBG("Can't calculate coordinates, get from mount");
        ans = write_cmd(":GS#", ibuff);
        lst = dups(ans, 1);
        if(!str2coord(ans, &LST)){
            if(++failcounter == 10){
                putlog("Lost connection with mount");
                DBG("Can't get LST!");
                signals(9);
            }
            DBG("Failed");
            return;
        }
        LST *= 15.*ERFA_DD2R; // convert hours to radians
    }else{
        lst = MALLOC(char, 32);
        r2sHMS(LST, lst, 32);
    }
    sMJD mjd;
    if(get_MJDt(NULL, &mjd)){
        ans = write_cmd(":GJD1#", ibuff);
        jd = dups(ans, 0);
    }else{
        jd = MALLOC(char, 32);
        snprintf(jd, 32, "%.10f", mjd.MJD);
    }
    polarCrds pNow = {.ra = r*15.*ERFA_DD2R, .dec = d*ERFA_DD2R}; // coordinates now
    horizCrds hNow;
    eq2hor(&pNow, &hNow, LST);
    failcounter = 0;
    tlast = time(NULL);
    // check it here, not in the beginning of function - to check connection with mount first
    if(!hdname){
        DBG("hdname not given!");
        return;
    }
    if(!elevation || !longitude || !latitude) getplace();
    ans = write_cmd(":GUDT#", ibuff);
    if(ans){
        char *comma = strchr(ans, ',');
        if(comma){
            *comma = 'T';
            date = dups(ans, 1);
        }
    }
    ans = write_cmd(":pS#", ibuff); pS = dups(ans, 1);
    ans = write_cmd(":Gstat#", ibuff);
    if(ans){
        mountstatus = atoi(ans);
        //DBG("Status: %d", mountstatus);
    }
    int l = strlen(hdname) + 7;
    char *aname = MALLOC(char, l);
    snprintf(aname, l, "%sXXXXXX", hdname);
    int fd = mkstemp(aname);
    if(fd < 0){
        WARN("Can't write header file: mkstemp()");
        FREE(aname);
        FREE(jd); FREE(lst); FREE(date); FREE(pS);
        return;
    }
    fchmod(fd, 0644);
    char val[22];
#define WRHDR(k, v, c)  do{if(printhdr(fd, k, v, c)){goto returning;}}while(0)
    WRHDR("TIMESYS", "'UTC'", "Time system");
    WRHDR("ORIGIN", "'SAO RAS'", "Organization responsible for the data");
    WRHDR("TELESCOP", TELESCOPE_NAME, "Telescope name");
    snprintf(val, 22, "%.10f", dut->px);
    WRHDR("POLARX", val, "IERS pole X coordinate, arcsec");
    snprintf(val, 22, "%.10f", dut->py);
    WRHDR("POLARY", val, "IERS pole Y coordinate, arcsec");
    snprintf(val, 22, "%.10f", dut->py);
    WRHDR("DUT1", val, "IERS `UT1-UTC`, sec");
    if(Target){ // target coordinates entered - store them @header
        snprintf(val, 22, "%.10f", ptRAdeg);
        WRHDR("TAGRA", val, "Target RA (J2000), degrees");
        snprintf(val, 22, "%.10f", ptDECdeg);
        WRHDR("TAGDEC", val, "Target DEC (J2000), degrees");
    }
    snprintf(val, 22, "%.10f", r*15.); // convert RA to degrees
    WRHDR("RA", val, "Telescope right ascension, current epoch, deg");
    snprintf(val, 22, "%.10f", d);
    WRHDR("DEC", val, "Telescope declination, current epoch, deg");
    snprintf(val, 22, "%.10f", hNow.az * ERFA_DR2D);
    WRHDR("AZ", val, "Telescope azimuth, current epoch, deg");
    snprintf(val, 22, "%.10f", hNow.zd * ERFA_DR2D);
    WRHDR("ZD", val, "Telescope zenith distance, current epoch, deg");
    WRHDR("TELSTAT", strstatus(mountstatus), "Telescope mount status");
    if(!get_MJDt(NULL, &mjd)){
           snprintf(val, 22, "%.10f", 2000.+(mjd.MJD-MJD2000)/365.25); // calculate EPOCH/EQUINOX
           WRHDR("EQUINOX", val, "Equinox of celestial coordinate system");
           if(!jd){
               snprintf(val, 22, "%.10f", mjd.MJD);
               WRHDR("MJD-END", val, "Modified julian date of observations end");
           }
       }
    if(jd){
        WRHDR("MJD-END", jd, "Modified julian date of observations end");
    }
    if(pS) WRHDR("PIERSIDE", pS, "Pier side of telescope mount");
    if(elevation) WRHDR("ELEVAT", elevation, "Elevation of site over the sea level");
    if(longitude) WRHDR("LONGITUD", longitude, "Geo longitude of site (east negative)");
    if(latitude) WRHDR("LATITUDE", latitude, "Geo latitude of site (south negative)");
    if(lst) WRHDR("LSTEND", lst, "Local sidereal time of observations end");
    if(date) WRHDR("DATE-END", date, "Date (UTC) of observations end");
    if(weather){
        snprintf(val, 22, "%.1f", weather->relhum);
        WRHDR("HUMIDITY", val, "Relative humidity, %%");
        snprintf(val, 22, "%.1f", weather->pres);
        WRHDR("PRESSURE", val, "Atmospheric pressure, mmHg");
        snprintf(val, 22, "%.1f", weather->tc);
        WRHDR("EXTTEMP", val, "External temperature, degrC");
        snprintf(val, 22, "%.0f", weather->rain);
        WRHDR("RAIN", val, "Rain conditions");
        snprintf(val, 22, "%.1f", weather->clouds);
        WRHDR("SKYQUAL", val, "Sky quality (0 - wery bad, >2500 - good)");
        snprintf(val, 22, "%.1f", weather->wind);
        WRHDR("WINDSPD", val, "Wind speed (m/s)");
        snprintf(val, 22, "%.0f", weather->time);
        WRHDR("WEATTIME", val, "Unix time of weather measurements");
    }
    // WRHDR("", , "");
#undef WRHDR
returning:
    FREE(jd); FREE(lst); FREE(date); FREE(pS);
    close(fd);
    rename(aname, hdname);
    FREE(aname);
}

// terminal thread: allows to work with terminal through socket
void *term_thread(void *sockd){
    int sock = *(int*)sockd;
    char buff[BUFLEN+1], ibuff[BUFLEN+2];
    // get client IP from socket fd - for logging
    struct sockaddr_in peer;
    socklen_t peer_len = sizeof(peer);
    char *peerIP = NULL;
    if(getpeername(sock, (struct sockaddr*)&peer, &peer_len) == 0){
        peerIP = inet_ntoa(peer.sin_addr);
    }
    while(!global_quit){ // blocking read
        ssize_t rd = read(sock, buff, BUFLEN);
        if(rd <= 0){ // error or disconnect
            DBG("Nothing to read from fd %d (ret: %zd)", sock, rd);
            break;
        }
        buff[rd] = 0;
        char *ch = strchr(buff, '\n');
        if(ch) *ch = 0;
        if(!buff[0]) continue; // empty string
        DBG("%s COMMAND: %s", peerIP, buff);
        if(strcasecmp(buff, "pause") == 0){
            DBG("PAUSED");
            putlog("Port writing outside terminal thread is paused");
            pause_communication = 1;
            continue;
        }
        if(strcasecmp(buff, "continue") == 0){
            DBG("CONTINUED");
            putlog("Port writing outside terminal thread is restored by user");
            pause_communication = 0;
            continue;
        }
        char *ans = write_cmd(buff, ibuff);
        putlog("%s COMMAND %s ANSWER %s", peerIP, buff, ibuff);
        DBG("ANSWER: %s", ibuff);
        if(ans){
            ssize_t l = (ssize_t)strlen(ans);
            if(l++){
                ans[l-1] = '\n';
                ans[l] = 0;
                if(l != write(sock, ans, l)){
                    WARN("term_thread, write()");
                    break;
                }
            }
        }
    }
    close(sock);
    return NULL;
}
