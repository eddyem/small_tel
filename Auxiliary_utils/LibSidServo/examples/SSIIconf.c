/*
 * This file is part of the libsidservo project.
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

#include "conf.h"
#include "sidservo.h"
#include "simpleconv.h"

typedef struct{
    int help;
    int helpargs;
    int writeconf;
    char *conffile;
    char *hwconffile;
} parameters;

static hardware_configuration_t HW = {0};

static parameters G = {0};

static sl_option_t cmdlnopts[] = {
    {"help",        NO_ARGS,    NULL,   'h',    arg_int,    APTR(&G.help),      "show this help"},
    {"help-opts",   NO_ARGS,    NULL,   'H',    arg_int,    APTR(&G.helpargs),  "configuration help"},
    {"serconf",     NEED_ARG,   NULL,   'C',    arg_string, APTR(&G.conffile),  "serial configuration file name"},
    {"hwconf",      NEED_ARG,   NULL,   'i',    arg_string, APTR(&G.hwconffile),"SSII configuration file name"},
    {"writeconf",   NO_ARGS,    NULL,   0,      arg_int,    APTR(&G.writeconf), "write configuration (BE CAREFUL!)"},
    end_option
};

static sl_option_t confopts[] = {
    {"Xaccel",      NEED_ARG,   NULL,   0,      arg_double, APTR(&HW.Xconf.accel),  "X Default Acceleration, rad/s^2"},
    {"Yaccel",      NEED_ARG,   NULL,   0,      arg_double, APTR(&HW.Yconf.accel),  "Y Default Acceleration, rad/s^2"},
    end_option
};

int main(int argc, char** argv){
    sl_init();
    sl_parseargs(&argc, &argv, cmdlnopts);
    if(G.help)
        sl_showhelp(-1, cmdlnopts);
    if(G.helpargs)
        sl_showhelp(-1, confopts);
    conf_t *sconf = readServoConf(G.conffile);
    if(!sconf){
        dumpConf();
        return 1;
    }
    if(MCC_E_OK != Mount.init(sconf)) ERRX("Can't init mount");
    if(MCC_E_OK != Mount.getHWconfig(&HW)) ERRX("Can't read configuration");
    char *c = sl_print_opts(confopts, TRUE);
    green("Got configuration:\n");
    printf("%s\n", c);
    FREE(c);
    if(G.hwconffile && G.writeconf){
        ;
    }
    Mount.quit();
    return 0;
}
