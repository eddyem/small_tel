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
#ifndef __SOCKET_H__
#define __SOCKET_H__

// time interval for data polling (seconds)
#define T_INTERVAL      (10.)
// max amount of opened fd (+1 for server socket)
#define MAX_FDS         (11)
// no data timeout
#define NODATA_TMOUT    (90.)

void daemonize(char *port);

#endif // __SOCKET_H__
