/*
 * This file is part of the weatherdaemon project.
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

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <string.h>
#include <stdlib.h>

// set flag FORCE_OFF only within FORCEOFF_PAUSE seconds after power loss
#define FORCEOFF_PAUSE  30

#include "weathlib.h"
#define SENSOR_NAME  "SNMP UPS monitor"

// https://mibs.observium.org/mib/XUPS-MIB/

// gcc $(net-snmp-config --cflags --libs) snmp.c -o snmptest

enum{
    NBATSTAT,
    NTONBAT,
    NTREMAIN,
    NBATCAP,
    NSOURCE,
    NONBAT,
    NAMOUNT
};

enum{
    BATT_STAT_UNKN = 1,
    BATT_STAT_NORMAL,
    BATT_STAT_LOW,
    BATT_STAT_DEPLETED,
    BATT_STAT_AMOUNT
};

const char* batt_stat[BATT_STAT_AMOUNT]= {
    "--",
    [BATT_STAT_UNKN] = "Unknown",
    [BATT_STAT_NORMAL] = "Normal",
    [BATT_STAT_LOW] = "Low",
    [BATT_STAT_DEPLETED] = "Depleted"
};

enum{
    SOURCE_OTHER = 1,
    SOURCE_NONE,
    SOURCE_NORMAL,
    SOURCE_BYPASS,
    SOURCE_BATTERY,
    SOURCE_BOOSTER,
    SOURCE_REDUCER,
    SOURCE_AMOUNT
};

const char *sources[SOURCE_AMOUNT] = {
    "--",
    [SOURCE_OTHER] = "Other",
    [SOURCE_NONE] = "None",
    [SOURCE_NORMAL] = "Normal",
    [SOURCE_BYPASS] = "Bypass",
    [SOURCE_BATTERY] = "Battery",
    [SOURCE_BOOSTER] = "Booster",
    [SOURCE_REDUCER] = "Reducer"
};

enum{
    OID_BATT_STATUS,        // batt status: 1 - unkn, 2 - normal, 3 - low, 4 - depleted
    OID_BATT_SECONDS_ONBAT, // seconds from ONBAT starts
    OID_BATT_EST_MINUTES,   // estimated minutes of work
    OID_BATT_CAPACITY,      // capacity of battery
    OID_OUTPUT_SOURCE,      // input source: 1 - other, 2 - none, 3 - normal, 4 - bypass, 5 - battery, 6 - booster, 7 - reducer
    OID_AMOUNT
};

static netsnmp_session *snmp_session;
static oid anOID[OID_AMOUNT][MAX_OID_LEN];
static size_t anOID_len[OID_AMOUNT];
static int running = 1;

const char *oids[OID_AMOUNT] = {
    [OID_BATT_STATUS] = ".1.3.6.1.2.1.33.1.2.1.0",
    [OID_BATT_SECONDS_ONBAT] = ".1.3.6.1.2.1.33.1.2.2.0",
    [OID_BATT_EST_MINUTES] = ".1.3.6.1.2.1.33.1.2.3.0",
    [OID_BATT_CAPACITY] = ".1.3.6.1.2.1.33.1.2.4.0",
    [OID_OUTPUT_SOURCE] = ".1.3.6.1.2.1.33.1.4.1.0",
};

static const val_t values[NAMOUNT] = {
    [NBATSTAT] = {.sense = VAL_RECOMMENDED, .type = VALT_STRING, .meaning = IS_OTHER, .name = "UPSBTST", .comment = "UPS battery status"},
    [NTONBAT] = {.sense = VAL_RECOMMENDED, .type = VALT_UINT, .meaning = IS_OTHER, .name = "UPSTONBT", .comment = "UPS worked on battery time (s)"},
    [NTREMAIN] = {.sense = VAL_RECOMMENDED, .type = VALT_UINT, .meaning = IS_OTHER, .name = "UPSTREM", .comment = "UPS estimated time on battery (s)"},
    [NBATCAP] = {.sense = VAL_RECOMMENDED, .type = VALT_UINT, .meaning = IS_OTHER, .name = "UPSBATCP", .comment = "UPS battery capacity, percents"},
    [NSOURCE] = {.sense = VAL_RECOMMENDED, .type = VALT_STRING, .meaning = IS_OTHER, .name = "UPSSRC", .comment = "UPS power source"},
    [NONBAT] = {.sense = VAL_FORCEDSHTDN, .type = VALT_UINT, .meaning = IS_FORCEDSHTDN, .name = "UPSONBAT", .comment = "AC power lost, works on battery"},
};

static void *mainthread(void *s){
    double t0 = sl_dtime();
    sensordata_t *sensor = (sensordata_t *)s;
    netsnmp_pdu *pdu, *response;
    while(running){
        //DBG("run");
        pdu = snmp_pdu_create(SNMP_MSG_GET);
        for(int i = 0; i < OID_AMOUNT; ++i)
            snmp_add_null_var(pdu, anOID[i], anOID_len[i]);
        int status = snmp_synch_response(snmp_session, pdu, &response);
        //DBG("status = %d", status);
        if(status == STAT_SUCCESS && response->errstat == SNMP_ERR_NOERROR){
            time_t curt = time(NULL);
            netsnmp_variable_list *vars = response->variables;
            pthread_mutex_lock(&sensor->valmutex);
            int ival = *vars->val.integer;
            if(ival > 0 && ival < BATT_STAT_AMOUNT)
                snprintf(sensor->values[NBATSTAT].value.str, STRT_LEN+1, "%s", batt_stat[ival]);
            vars = vars->next_variable;
            uint32_t tonbat = (uint32_t) *vars->val.integer;
            sensor->values[NTONBAT].value.u = tonbat;
            vars = vars->next_variable;
            sensor->values[NTREMAIN].value.u = 60 * (uint32_t) *vars->val.integer;
            vars = vars->next_variable;
            sensor->values[NBATCAP].value.u = (uint32_t) *vars->val.integer;
            vars = vars->next_variable;
            ival = *vars->val.integer;
            if(ival > 0 && ival < SOURCE_AMOUNT)
                snprintf(sensor->values[NSOURCE].value.str, STRT_LEN+1, "%s", sources[ival]);
            if(ival ==  SOURCE_BATTERY && tonbat > FORCEOFF_PAUSE){
                sensor->values[NONBAT].value.u = 1;
            }else sensor->values[NONBAT].value.u = 0;
            for(int i = 0; i < NAMOUNT; ++i)
                sensor->values[i].time = curt;
            pthread_mutex_unlock(&sensor->valmutex);
            if(sensor->freshdatahandler) sensor->freshdatahandler(sensor);
        }else DBG("Error in packet");
        if(response) snmp_free_pdu(response);
        //DBG("sleep");
        while(sl_dtime() - t0 < sensor->tpoll) usleep(500);
        t0 = sl_dtime();
    }
    return NULL;
}

static void snmp_kill(sensordata_t *s){
    running = 0;
    pthread_join(s->thread, NULL);
    snmp_close(snmp_session);
    common_kill(s);
}

sensordata_t *sensor_new(int N, time_t pollt, const char *descr){
    FNAME();
    if(!descr || !*descr) return NULL;

    netsnmp_session session;
    init_snmp("snmpapp");

    snmp_sess_init(&session);
    session.version = SNMP_VERSION_1;
    session.community = (u_char *)"public";
    session.community_len = strlen((const char *)session.community);

    const char *colon = strchr(descr, ':');
    if(colon) descr = colon + 1; // omit "N:" in field "N:host"
    session.peername = strdup(descr);

    snmp_session = snmp_open(&session);
    if(!snmp_session){
        snmp_sess_perror("snmp_open", &session);
        FREE(session.peername);
        return NULL;
    }

    sensordata_t *s = common_new();
    if(!s){
        snmp_close(snmp_session);
        return NULL;
    }
    s->kill = snmp_kill;

    snprintf(s->name, NAME_LEN, "%s @ %s", SENSOR_NAME, descr);
    s->PluginNo = N;
    s->fdes = -1;
    s->Nvalues = NAMOUNT;
    if(pollt) s->tpoll = pollt;
    s->values = MALLOC(val_t, NAMOUNT);
    for(int i = 0; i < NAMOUNT; ++i) s->values[i] = values[i];

    DBG("init OIDs");
    for(int i = 0; i < OID_AMOUNT; ++i){
        anOID_len[i] = MAX_OID_LEN;
        if(!read_objid(oids[i], anOID[i], &anOID_len[i])){
            snmp_perror(oids[i]);
            continue;
        }
        DBG("Got OID %s", oids[i]);
        //snmp_add_null_var(snmp_pdu, anOID, anOID_len);
    }
    DBG("Start main thread");
    if(!(s->ringbuffer = sl_RB_new(BUFSIZ)) ||
        pthread_create(&s->thread, NULL, mainthread,  (void*)s)){
        s->kill(s);
        return NULL;
    }
    return s;
}
