/*                                                                                                  geany_encoding=koi8-r
 * cmdlnopts.h - comand line options for parceargs
 *
 * Copyright 2013 Edward V. Emelianoff <eddy@sao.ru>
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
 */

#pragma once
#ifndef __CMDLNOPTS_H__
#define __CMDLNOPTS_H__

/*
 * here are some typedef's for global data
 */
typedef struct{
    char *device;           // serial device name
    char *port;             // port to connect
    char *dbgport;          // port for debug console
    char *pidfile;          // name of PID file
    char *logfile;          // logging to this file
    char *crdsfile;         // file where FITS-header should be written
    char *weathserver;      // weather server name
    int emulation;          // run in emulation mode
    int rest_pars_num;      // number of rest parameters
    int weathport;          // weather server port
    char** rest_pars;       // the rest parameters: array of char*
} glob_pars;

// global parameters
extern glob_pars *GP;

glob_pars *parse_args(int argc, char **argv);
#endif // __CMDLNOPTS_H__
