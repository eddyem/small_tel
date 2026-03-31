/*
 * This file is part of the teldaemon project.
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

#pragma once

#include <stdint.h>

typedef union{
    struct{
    uint8_t telname : 1;    // show telescope name
    uint8_t fosuser : 1;    // show focuser status
    uint8_t cooler  : 1;    // show cooler status
    uint8_t heater  : 1;    // show heater status
    uint8_t exttemp : 1;    // show external temperature
    uint8_t mirtemp : 1;    // show mirror temperature
    uint8_t meastime: 1;    // show measurement time
    };
    uint8_t flags;          // alltogether as single flags
} header_mask_t;

const char *getheadermaskhelp();
void write_header();
int header_create(const char *file, int flags);
void telname(const char *name);
