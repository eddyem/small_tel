/*
 * This file is part of the libsidservo project.
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

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <usefull_macros.h>

#define XYBUFSZ     (2048)

struct{
    int help;
    char *Xpath;
    char *Ypath;
    double dt;
} G = {
    .Xpath = "/dev/encoder_X0",
    .Ypath = "/dev/encoder_Y0",
    .dt = 0.001,
};

sl_option_t options[] = {
    {"help",    NO_ARGS,    NULL,   'h',    arg_int,    APTR(&G.help),      "show this help"},
    {"Xpath",   NEED_ARG,   NULL,   'X',    arg_string, APTR(&G.Xpath),     "path to X encoder"},
    {"Ypath",   NEED_ARG,   NULL,   'Y',    arg_string, APTR(&G.Ypath),     "path to Y encoder"},
    {"dt",      NEED_ARG,   NULL,   'd',    arg_double, APTR(&G.dt),        "request interval (1e-4..10s)"},
};

typedef struct{
    char buf[XYBUFSZ+1];
    int len;
} buf_t;

static int Xfd = -1, Yfd = -1;

void signals(int sig){
    if(sig){
        signal(sig, SIG_IGN);
        DBG("Get signal %d, quit.\n", sig);
    }
    DBG("close");
    if(Xfd > 0){ close(Xfd); Xfd = -1; }
    if(Yfd > 0){ close(Yfd); Yfd = -1; }
    exit(sig);
}

static int op(const char *nm){
    int fd = open(nm, O_RDWR|O_NOCTTY|O_NONBLOCK);
    if(fd < 0) ERR("Can't open %s", nm);
    struct termios2 tty;
    if(ioctl(fd, TCGETS2, &tty)) ERR("Can't read TTY settings");
    tty.c_lflag     = 0; // ~(ICANON | ECHO | ECHOE | ISIG)
    tty.c_iflag     = 0; // don't do any changes in input stream
    tty.c_oflag     = 0; // don't do any changes in output stream
    tty.c_cflag     = BOTHER | CS8 | CREAD | CLOCAL; // other speed, 8bit, RW, ignore line ctrl
    tty.c_ispeed = 1000000;
    tty.c_ospeed = 1000000;
    if(ioctl(fd, TCSETS2, &tty)) ERR("Can't set TTY settings");
    // try to set exclusive
    if(ioctl(fd, TIOCEXCL)){DBG("Can't make exclusive");}
    return fd;
}

static int eolcnt(buf_t *buf){
    if(!buf) return -1;
    int cnt = 0;
    for(int i = 0; i < buf->len; ++i)
        if(buf->buf[i] == '\n') ++cnt;
    return cnt;
}
// move last record (if any) into head of buffer
static void movelast(buf_t *buf){
    FNAME();
    if(!buf) return;
    DBG("buf was: %s", buf->buf);
    int cnt = eolcnt(buf);
    char *E = strrchr(buf->buf, '\n');
    int idx = -1;
    if(E){
        idx = ++E - buf->buf; // position of symbol after '\n'
    }else{
        buf->len = strlen(buf->buf);
        DBG("leave as is (%s)", buf->buf);
        return;
    }
    DBG("cnt=%d, idx=%d", cnt, idx);
    switch(cnt){
        case 0: // EOL not found - clear buf
            buf->len = 0;
        break;
        case 1: // only one record - move all after '\n'
            if(idx > 0 && idx < XYBUFSZ){
                buf->len = XYBUFSZ - idx;
                memmove(buf->buf, E, buf->len);
            }else buf->len = 0;
        break;
        default: // more than one record - move
        {
            int i = idx - 2;
            for(; i > -1; --i)
                if(buf->buf[i] == '\n') break;
            ++i;
            buf->len = XYBUFSZ - i;
            memmove(buf->buf, &buf->buf[i], buf->len);
        }
    }
    buf->buf[buf->len] = 0;
    DBG("MOVED; now buf[%d]=%s", buf->len, buf->buf);
}
// write to buffer next data portion; return FALSE in case of error
static int readstrings(buf_t *buf, int fd){
    if(!buf){WARNX("Empty buffer"); return FALSE;}
    int L = XYBUFSZ - buf->len;
    if(L == 0){
        DBG("len: %d", buf->len);
        movelast(buf);
        L = XYBUFSZ - buf->len;
    }
    int got = read(fd, &buf->buf[buf->len], L);
    if(got < 0){
        WARN("read()");
        return FALSE;
    }else if(got == 0) return TRUE;
    buf->len += got;
    buf->buf[buf->len] = 0;
    DBG("buf[%d]: %s", buf->len, buf->buf);
    return TRUE;
}
// return TRUE if got, FALSE if no data found
static int getdata(buf_t *buf, long *out){
    if(!buf) return -1;
    // read record between last '\n' and previous (or start of string)
    int cnt = eolcnt(buf);
    if(cnt < 1) return FALSE;
    char *last = strrchr(buf->buf, '\n');
    if(!last) return FALSE; // WTF?
    *last = 0;
    char *prev = buf->buf;
    if(cnt > 1) prev = strrchr(buf->buf, '\n') + 1;
    if(!prev) prev = buf->buf; // ??
    if(out) *out = atol(prev);
    int l = strlen(++last);
    if(l < XYBUFSZ){
        buf->len = l;
        if(l){
            memmove(buf->buf, last, l);
            DBG("moved: %s", buf->buf);
        }else DBG("empty line");
    }else{
        buf->len = 0;
        DBG("buffer clear");
    }
    return TRUE;
}
// try to write '\n' asking new data portion; return FALSE if failed
static int asknext(int fd){
    FNAME();
    if(fd < 0) return FALSE;
    int i = 0;
    for(; i < 5; ++i){
        int l = write(fd, "\n", 1);
        DBG("l=%d", l);
        if(1 == l) return TRUE;
        usleep(100);
    }
    DBG("5 tries... failed!");
    return FALSE;
}

int main(int argc, char **argv){
    buf_t xbuf, ybuf;
    long xlast, ylast;
    double xtlast, ytlast;
    sl_init();
    sl_parseargs(&argc, &argv, options);
    if(G.help) sl_showhelp(-1, options);
    if(G.dt < 1e-4) ERRX("dx too small");
    if(G.dt > 10.) ERRX("dx too big");
    Xfd = op(G.Xpath);
    Yfd = op(G.Ypath);
    struct pollfd pfds[2];
    pfds[0].fd = Xfd; pfds[0].events = POLLIN;
    pfds[1].fd = Yfd; pfds[1].events = POLLIN;
    double t0x, t0y, tstart;
    asknext(Xfd); asknext(Yfd);
    t0x = t0y = tstart = sl_dtime();
    DBG("Start");
    do{ // main cycle
        if(poll(pfds, 2, 1) < 0){
            WARN("poll()");
            break;
        }
        if(pfds[0].revents && POLLIN){
            if(!readstrings(&xbuf, Xfd)) break;
        }
        if(pfds[1].revents && POLLIN){
            if(!readstrings(&ybuf, Yfd)) break;
        }
        double curt = sl_dtime();
        if(getdata(&xbuf, &xlast)) xtlast = curt;
        if(curt - t0x >= G.dt){ // get last records
            if(curt - xtlast < 1.5*G.dt)
                printf("%-14.4fX=%ld\n", xtlast-tstart, xlast);
            if(!asknext(Xfd)) break;
            t0x = (curt - t0x < 2.*G.dt) ? t0x + G.dt : curt;
        }
        curt = sl_dtime();
        if(getdata(&ybuf, &ylast)) ytlast = curt;
        if(curt - t0y >= G.dt){ // get last records
            if(curt - ytlast < 1.5*G.dt)
                printf("%-14.4fY=%ld\n", ytlast-tstart, ylast);
            if(!asknext(Yfd)) break;
            t0y = (curt - t0y < 2.*G.dt) ? t0y + G.dt : curt;
        }
    }while(Xfd > 0 && Yfd > 0);
    DBG("OOps: disconnected");
    signals(0);
    return 0;
}
