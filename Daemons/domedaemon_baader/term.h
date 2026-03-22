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

// text commands and answers
#define TXT_GETWARN     "d#warning\n"
#define TXT_OPENDOME    "d#opendom\n"
#define TXT_CLOSEDOME   "d#closdom\n"
#define TXT_STOPDOME    "d#stopdom\n"
#define TXT_GETSTAT     "d#get_dom\n"
#define TXT_GETWEAT     "d#ask_wea\n"
#define TXT_ANS_WEAT    "d#wea"
#define TXT_ANS_STAT    "d#pos"
#define TXT_ANS_ERR     "d#erro"

int open_term(char *path, int speed, double usec);
void close_term();
int read_term(char *buf, int length);
int write_term(const char *buf, int length);
int write_cmd(const char *buf);
