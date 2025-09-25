/*
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

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <usefull_macros.h>

static sl_tty_t *dev = NULL;
static const char *wro = "(wrong parameter)";

typedef struct{
    int help;
    char *path;
    char *customcmd;
    char *getstatus;
    int baudrate;
    int status;
} globopts;

typedef struct{
    int all;
    int help;
    int rating;
    int flag;
    int status;
    int mode;
} statusinfo;

typedef void (*parsefn)(const char *answer);

static globopts G = {
    .path = "/dev/ttyS0",
    .baudrate = 2400,
};

static statusinfo S = {0};

static sl_option_t opts[] = {
    {"help",    NO_ARGS,    NULL,   'h',    arg_int,    APTR(&G.help),      "show this help"},
    {"devpath", NEED_ARG,   NULL,   'd',    arg_string, APTR(&G.path),      "path to device (default: /dev/ttyS0)"},
    {"baudrate",NEED_ARG,   NULL,   'b',    arg_int,    APTR(&G.baudrate),  "baudrate (default: 2400)"},
    {"cmd",     NEED_ARG,   NULL,   'c',    arg_string, APTR(&G.customcmd), "custom command to send"},
    {"status",  NEED_ARG,   NULL,   's',    arg_string, APTR(&G.getstatus), "get status (type 'help' for options)"},
    end_option
};

static sl_suboption_t sopts[] = {
    {"all",     NO_ARGS,    arg_int,    APTR(&S.all)},
    {"help",    NO_ARGS,    arg_int,    APTR(&S.help)},
    {"rating",  NO_ARGS,    arg_int,    APTR(&S.rating)},
    {"flag",    NO_ARGS,    arg_int,    APTR(&S.flag)},
    {"status",  NO_ARGS,    arg_int,    APTR(&S.status)},
    {"mode",    NO_ARGS,    arg_int,    APTR(&S.mode)},
    end_suboption
};

static void gettershelp(){
    fprintf(stderr, "Status parameters:\n");
    fprintf(stderr, "all - show all information available\n");
    fprintf(stderr, "rating - device rating information (QPIRI)\n");
    fprintf(stderr, "flag - device flag status (QFLAG)\n");
    fprintf(stderr, "status - device general status parameters (QPIGS)\n");
    fprintf(stderr, "mode - device mode (QMOD)\n");
//  fprintf(stderr, "\n");
}

static uint8_t *cal_crc(const char *cmd, int len){
    static uint8_t CRC[3]; // 0 - hi, 1 - low
    if(!cmd || len < 1) return 0;
    const uint16_t crc_table[16] = {
        0x0000,0x1021,0x2042,0x3063,0x4084,0x50a5,0x60c6,0x70e7,
        0x8108,0x9129,0xa14a,0xb16b,0xc18c,0xd1ad,0xe1ce,0xf1ef
    };
    uint16_t crc = 0;
    uint8_t *ptr = (uint8_t*)cmd;
    while(len--){
        uint8_t da = ((uint8_t)(crc >> 8)) >> 4;
        crc <<= 4;
        crc ^= crc_table[da ^ (*ptr >> 4)];
        da = ((uint8_t)(crc >> 8)) >> 4;
        crc <<= 4;
        crc ^= crc_table[da ^ (*ptr & 0x0f)];
        ptr++;
    }
    uint8_t bCRCLow = crc;
    uint8_t bCRCHign= (uint8_t)(crc >> 8);
    if(bCRCLow==0x28 || bCRCLow==0x0d || bCRCLow==0x0a) ++bCRCLow;
    if(bCRCHign==0x28 || bCRCHign==0x0d || bCRCHign==0x0a) ++bCRCHign;
    CRC[0] = bCRCHign; CRC[1] = bCRCLow; CRC[2] = '\r';
    DBG("CRC: 0x%02X 0x%02X 0x%02X", CRC[0], CRC[1], CRC[2]);
    return CRC;
}

static int sendcmd(const char *cmd){
    if(!cmd || !*cmd || !dev) return 0;
    int len = strlen(cmd);
    uint8_t *CRC = cal_crc(cmd, len);
#ifdef EBUG
    printf("COMMAND: ");
    for(int i = 0; i < len; ++i) printf("0x%02X ", cmd[i]);
    printf("\n");
#endif
    if(sl_tty_write(dev->comfd, cmd, len)){
        WARN("Can't write command");
        return 0;
    }
    if(sl_tty_write(dev->comfd, (const char*) CRC, 3)){
        WARN("Can't write CRC & STREND");
        return 0;
    }
    DBG("Command %s sent", cmd);
    return 1;
}

static char *rd(){
    if(!dev) return NULL;
    int got = sl_tty_read(dev);
    if(got < 0) ERR("Can't read");
    if(dev->buflen < 3) return NULL;
    uint8_t *CRC = cal_crc(dev->buf, dev->buflen - 3);
    DBG("GOT CRC: 0x%02X 0x%02X", (uint8_t)dev->buf[dev->buflen-3], (uint8_t)dev->buf[dev->buflen-2]);
    if(CRC[0] != (uint8_t)dev->buf[dev->buflen-3] || CRC[1] != (uint8_t)dev->buf[dev->buflen-2]){
        WARNX("Bad CRC");
        return NULL;
    }
    char *r = dev->buf + 1;
    dev->buf[dev->buflen-3] = 0;
    return r;
}
// 230.0 13.0 230.0 50.0 13.0 3000 3000 24.0 23.0 21.0 28.2 27.0 0 25 50 0 0 2 - 01 1 0 27.0 0 0
// b      c    d     e   f      h    i   j1  k1    j2   k2   l   o1 p1q0 o2p2q2r s  t u  v   w x
static void ratingparsing(const char *str){
    float b, c, d, e, f, j1, k1, j2, k2, l, v;
    int h, i, o1, p1, q, o2, p2, q2, s, t, u, w, x;
    char r;
    int N = sscanf(str, "%f %f %f %f %f %d %d %f %f %f %f %f %d %d %d %d %d %d %c %d %d %d %f %d %d",
        &b, &c, &d, &e, &f, &h, &i, &j1, &k1, &j2, &k2, &l, &o1, &p1, &q, &o2, &p2, &q2, &r, &s, &t,
                   &u, &v, &w, &x);
    if(N != 25){ WARNX("Got not full answer (%d instead of 25): '%s'", N, str); return; }
    printf("\nGrid rating voltage: %g\nGrid rating current: %g\nAC optuput rating voltage: %g\n", b, c, d);
    printf("AC output rating frequency: %g\nAC output rating current: %g\nAC output rating apparent power: %d\n", e, f, h);
    printf("AC output rating active power: %d\nBattery rating voltage: %g\nBattery recharge voltage: %g\n", i, j1, k1);
    printf("Battery undervoltage: %g\nBattery bulk voltage: %g\nBattery float voltage: %g\n", j2, k2, l);
    static const char *types[] = {"AGM", "Flooded", "User"};
    printf("Battery type: %s\n", (o1 > -1 && o1 < 3) ? types[o1] : wro);
    printf("Current max AC charging current: %d\nCurrent max charging current: %d\n", p1, q);
    printf("Input voltage range: %s\n", o2 ? "UPS" : "Appliance");
    static const char *oprio[] = {"Utility", "Solar", "SBU"};
    printf("Output source priority: %s\n", (p2 > -1 && p2 < 3) ? oprio[p2] : wro);
    static const char *sprio[] = {"Utility", "Solar", "Solar+Utility", "Only solar charging"};
    printf("Charger source priority: %s\n", (q2 > -1 && q2 < 4) ? sprio[q2] : wro);
    printf("Parallel max num: %c\n", r);
    printf("Machine type: ");
    switch(s){
        case 0: printf("Grid tie"); break;
        case 1: printf("Off grid"); break;
        case 10: printf("Hybrid"); break;
        default: printf("%s", wro);
    }
    printf("\nTopology: %s\n", (t) ? "transformer" : "transformerless");
    printf("Output mode: %d\nBattery redischarge voltage: %g\n", u, v);
    printf("PV OK condition for parallel: %s\n", (w) ? "Only all connected" : "At least one connected");
    printf("PV power balance: %s\n\n", (x) ? "Sum of powers" : "Max charged current");
}

static void flagparsing(const char *str){
    char c;
    int first = 0;
    while((c = *str++)){
        if(c == 'D'){ red("\nDISABLED: "); first = 1; }
        else if(c == 'E'){ green("\nENABLED: "); first = 1; }
        else{
            const char *field = "unknown";
            switch(c){
                case 'a': field = "buzzer"; break;
                case 'b': field = "bypass"; break;
                case 'j': field = "power saving"; break;
                case 'k': field = "LCD display escape 1min"; break;
                case 'u': field = "overload restart"; break;
                case 'v': field = "over temperature restart"; break;
                case 'x': field = "backlight on"; break;
                case 'y': field = "alarm on interrupt"; break;
                case 'z': field = "fault code record"; break;
            }
            printf("%s%s", (first) ? "" : ", ", field);
            first = 0;
        }
    }
    printf("\n");
}

// 230.0 50.0 232.0 50.0 0000 0000 000 409 26.99 000 100 0489 0000 000.0 00.00 00000 10011101 00 03 00000 100
// b      c    d     e    f    g    h   i   j     k   o   t    e1    u    w      p     x       [unknown shit]
static void statusparsing(const char *str){
    float b, c, d, e, j, u, w;
    int f, g, h, i, k, o, t, e1, p, S, H, I, T;
    //int l = strlen(str);
    //for(int i = 0; i < l; ++i){char c = str[i]; if(isalnum(c)||c==' '||c=='.') printf("%c", c); else printf("\\x%02X", c); }
    char x[9];
    int N = sscanf(str, "%f %f %f %f %d %d %d %d %f %d %d %d %d %f %f %d %8s %d %d %d %d",
                    &b, &c, &d, &e, &f, &g, &h, &i, &j, &k, &o, &t, &e1, &u, &w, &p, x, &S, &H, &I, &T);
    DBG("N=%d", N);
    if(N >= 17){
        printf("Grid voltage: %g\nGrid frequency: %g\nAC output voltage: %g\nAC output frequency: %g\n",
               b, c, d, e);
        printf("AC output apparent power: %d\nAC output active power: %d\nOutput load percent: %d\n", f, g, h);
        printf("Bus voltage: %d\nBattery voltage: %g\nBattery charging current: %d\nBattery capacity: %d\n", i, j, k, o);
        printf("Inverter heat sink temperature: %d\nPV input current for battery: %d\nPV input voltage 1: %g\n", t, e1, u);
        printf("Battery voltage from SCC: %g\nBattery discharge current: %d\nDevice status:\n", w, p);
        static const char *sf[] = {"AC charging", "SCC charging", "Charging", "Steady batt voltage while charging",
                                "Load status", "SCC firmware updated", "configuration changed", "SBU priority version"};
        for(int i = 0; i < 8; ++i) printf("\t%s: %s\n", sf[i], x[i]=='1' ? "on/yes" : "off/no");
        if(N > 17){
            printf("Unknown values: %d, %d, %d, %d\n", S, H, I, T);
        }
    }else WARNX("Get not full answer: %d instead of 17", N);
}

static void modeparsing(const char *str){
    printf("Device mode: ");
    switch(*str){
        case 'B': printf("Battery\n"); break;
        case 'F': printf("Fault\n"); break;
        case 'H': printf("Power saving\n"); break;
        case 'L': printf("Line\n"); break;
        case 'P': printf("Power on\n"); break;
        case 'S': printf("Standby\n"); break;
        default: printf("Unknown");
    }
    printf("\n");
}

static void runparsing(const char *cmd, parsefn f){
    if(sendcmd(cmd)){
        char *got = rd();
        if(got) f(got);
    }
}

static void showstatus(){
    if(S.all || S.rating) runparsing("QPIRI", ratingparsing);
    if(S.all || S.flag) runparsing("QFLAG", flagparsing);
    if(S.all || S.status) runparsing("QPIGS", statusparsing);
    if(S.all || S.mode) runparsing("QMOD", modeparsing);
}

int main(int argc, char **argv){
    sl_init();
    sl_parseargs(&argc, &argv, opts);
    if(G.help) sl_showhelp(-1, opts);
    if(!G.path || G.baudrate < 1) ERRX("Need device path and baudrate");
    dev = sl_tty_new(G.path, G.baudrate, 128);
    if(!dev) ERR("Can't init serial device");
    dev = sl_tty_open(dev, 1);
    if(!dev) ERR("Can't open %s", G.path);
    if(sl_tty_tmout(100000)) WARNX("Can't set timeout");
    DBG("bufsz: %zd", dev->bufsz);
    if(G.getstatus){
        if(!sl_get_suboption(G.getstatus, sopts) || S.help){
            gettershelp();
            return 1;
        }
        showstatus();
    }
    if(G.customcmd){
        green("Try to send '%s'\n", G.customcmd);
        if(sendcmd(G.customcmd)){
            char *got = rd();
            if(got) printf("Get data: '%s'\n", got);
        }
    }
    sl_tty_close(&dev);
    return 0;
}
