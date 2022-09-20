/*
 * This file is part of the Hydreon_RG11 project.
 * Copyright 2022 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <usefull_macros.h>

#include "cmdlnopts.h"
#include "hydreon.h"

// selected registers threshold over old value
#define VAL_THRESHOLD   (5)

// check deviation of values over threshold; return TRUE if |x1-x0|>VAL_THRESHOLD
static int deviat(uint8_t x0, uint8_t x1){
    register uint8_t diff = (x0 > x1) ? x0 - x1 : x1 - x0;
    if(diff > VAL_THRESHOLD) return TRUE;
    return FALSE;
}

static glob_pars *G = NULL;
static FILE *outf = NULL;

void signals(int sig){
    if(sig > 0){
        signal(sig, SIG_IGN);
        DBG("Get signal %d, quit.\n", sig);
    }
    LOGERR("Exit with status %d", sig);
    if(G && G->pidfile) // remove unnesessary PID file
        unlink(G->pidfile);
    if(outf) fclose(outf);
    hydreon_close();
    exit(sig);
}

static void dumpRchanges(rg11 *new, rg11 *old){
    DBG("Regular changed");
    uint8_t *n = (uint8_t*) new, *o = (uint8_t*) old;
    int start = 1;
    for(int i = 0; i < RREGNUM; ++i){
        if(o[i] != n[i]){
            sl_putlogt(start, globlog, LOGLEVEL_MSG, "%s=%d", regname(i), n[i]);
            DBG("%s=%d", regname(i), n[i]);
            if(start) start = 0;
        }
    }
    uint8_t xOr = new->RGBits ^ old->RGBits;
    start = 1;
    if(xOr){
        uint8_t f = 1;
        for(int i = 0; i < RGBITNUM; ++i, f <<= 1){
            if(xOr & f){
                sl_putlogt(start, globlog, LOGLEVEL_MSG, "%s=%d", rgbitname(i), (new->RGBits & f) ? 1 : 0);
                DBG("%s=%d", rgbitname(i), (new->RGBits & f) ? 1 : 0);
                if(start) start = 0;
            }
        }
    }
}

static void dumpSchanges(slowregs *new, slowregs *old){
    DBG("Slow changed");
    uint8_t *n = (uint8_t*) new, *o = (uint8_t*) old;
    int start = 1;
    for(int i = 0; i < SREGNUM; ++i){
        if(o[i] != n[i]){
            sl_putlogt(start, globlog, LOGLEVEL_MSG, "%s=%d", slowname(i), n[i]);
            DBG("%s=%d", slowname(i), n[i]);
            if(start) start = 0;
        }
    }
}

static void puttotable(rg11 *R, slowregs *S){
    if(!R || !S){ // init -> plot header
        fprintf(outf, "%12s%8s%8s%8s%8s" \
                "%10s%8s%8s%10s" \
                "%8s%10s%8s%8s%10s%8s%8s%8s%8s%8s" \
                "\n",
                "UNIX time", "PeakRS", "SPeakRS", "RainAD8", "LRA",
                "PkOverThr", "Raining", "Freeze", "Out1OnCtr",
                "EmLevel", "RecEmStr", "TmprtrC", "ClearTR", "AmbLight", "Bucket", "Barrel", "DwellT", "MonoStb", "LightAD");
        return;
    }
    // old values: we will make dump of selected fields only if |new-old| > threshold
    static rg11 oRregs = {0};
    static slowregs oSregs = {0};
    static size_t Out1ctr = 0;
    int changed = FALSE;
    // now check selected fields
#define CHKR(r) do{if(deviat(oRregs.r, R->r)) changed = TRUE; oRregs.r = R->r;}while(0)
#define CHKS(r) do{if(deviat(oSregs.r, S->r)) changed = TRUE; oSregs.r = S->r;}while(0)
#define RGMASK  (PkOverThr | Raining | Out1On | Freeze)
    CHKR(PeakRS);
    CHKR(SPeakRS);
    CHKR(RainAD8);
    CHKR(LRA);
    if(!(oRregs.RGBits & Out1On) && (R->RGBits & Out1On)) ++Out1ctr;
    if((oRregs.RGBits & RGMASK) != (R->RGBits & RGMASK)){
        changed = TRUE;
        oRregs.RGBits = R->RGBits;
    }
    CHKS(EmLevel);
    CHKS(RecEmStr);
    CHKS(TmprtrF);
    CHKS(ClearTR);
    CHKS(AmbLight);
    CHKS(Bucket);
    CHKS(Barrel);
    CHKS(DwellT);
    CHKS(MonoStb);
    CHKS(LightAD);
#undef CHKR
#undef CHKS
#undef RGMASK
    if(!changed) return;
    fprintf(outf, "%12ld%8u%8u%8u%8u" \
                  "%10u%8u%8u%10zd" \
                  "%8u%10u%6.1f%8u%10u%8u%8u%8u%8u%8u" \
                  "\n",
            //  "UNIX time", "PeakRS", "SPeakRS", "RainAD8", "LRA",
            time(NULL), R->PeakRS, R->SPeakRS, R->RainAD8, R->LRA,
            // "PkOverThr", "Raining", "Freeze", "Out1OnCtr",
            (R->RGBits & PkOverThr) ? 255:0, (R->RGBits & Raining) ? 255:0, (R->RGBits & Freeze) ? 255:0, Out1ctr,
            // "EmLevel", "RecEmStr", "TmprtrC", "ClearTR", "AmbLight", "Bucket", "Barrel", "DwellT", "MonoStb", "LightAD"
            S->EmLevel, S->RecEmStr, (S->TmprtrF - 32.)*5./9., S->ClearTR, S->AmbLight, S->Bucket, S->Barrel, S->DwellT, S->MonoStb, S->LightAD
            );
    fflush(outf);
}

int main(int argc, char **argv){
    initial_setup();
    char *self = strdup(argv[0]);
    G = parse_args(argc, argv);
    if(G->timeout < 5) ERRX("Timeout should be not less than 5 seconds");
    if(!G->logfile && !G->outfile) ERRX("Point at least log or output file name");
    check4running(self, G->pidfile);
    if(!hydreon_open(G->device)) return 1;
    if(G->logfile) OPENLOG(G->logfile, LOGLEVEL_ANY, 0);
    if(G->outfile){
        outf = fopen(G->outfile, "w");
        if(!outf) ERR("Can't open file %s", G->outfile);
    }
    rg11 Rregs, oRregs = {0};
    slowregs Sregs, oSregs = {0};
    signal(SIGTERM, signals); // kill (-15) - quit
    signal(SIGHUP, SIG_IGN);  // hup - ignore
    signal(SIGINT, signals);  // ctrl+C - quit
    signal(SIGQUIT, signals); // ctrl+\ - quit
    signal(SIGTSTP, SIG_IGN); // ignore ctrl+Z
    double t0 = dtime();
    puttotable(NULL, NULL);
    while(dtime() - t0 < (double)G->timeout){ // dump only changes
        if(!hydreon_getpacket(&Rregs, &Sregs)) continue;
        int changes = FALSE;
        if(memcmp(&Rregs, &oRregs, RREGNUM + 1)){ // Rregs changed -> log changes
            dumpRchanges(&Rregs, &oRregs);
            memcpy(&oRregs, &Rregs, sizeof(rg11));
            changes = TRUE;
        }
        if(memcmp(&Sregs, &oSregs, sizeof(slowregs))){ // Sregs changed -> log
            dumpSchanges(&Sregs, &oSregs);
            memcpy(&oSregs, &Sregs, sizeof(slowregs));
            changes = TRUE;
        }
        if(changes) puttotable(&Rregs, &Sregs);
        t0 = dtime();
    }
    signals(-1); // never reached
    return 0;
}
