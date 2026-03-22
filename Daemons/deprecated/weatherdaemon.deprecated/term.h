/*
 * This file is part of the weatherdaemon project.
 * Copyright 2021 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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

#pragma once
#ifndef __TERM_H__
#define __TERM_H__

#include <usefull_macros.h>

#define FRAME_MAX_LENGTH        (300)
#define MAX_MEMORY_DUMP_SIZE    (0x800 * 4)
// Terminal timeout (seconds)
#define     WAIT_TMOUT          (0.5)
// Terminal polling timeout - 1 second
#define     T_POLLING_TMOUT     (1.0)

extern TTY_descr *ttydescr;
void run_terminal();
int try_connect(char *device, int baudrate);
char *poll_device();

#endif // __TERM_H__
