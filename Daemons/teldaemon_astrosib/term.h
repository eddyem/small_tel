/*
 * This file is part of the baader_dome project.
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

// pause before reading answer: for stupid baader = 50ms
#define USLEEP_BEFORE_READ  50000

// length of answer (including terminating zero)
#define ANSLEN  128

int term_open(char *path, int speed, double usec);
void term_close();
char *term_read(char ans[ANSLEN]);
char *term_write(const char *str, char ans[ANSLEN]);
char *term_cmdwans(const char *str, const char *prefix, char ans[ANSLEN]);
