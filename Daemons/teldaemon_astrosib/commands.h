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

// text commands and answers
#define TXT_FOCUSABS    "FOCUSERGO?"
#define TXT_FOCUSIN     "FOCUSERI?"
#define TXT_FOCUSOUT    "FOCUSERO?"
#define TXT_FOCSTOP     "FOCUSERSTOP?\r"
#define TXT_FOCGET      "FOCUSERGPOS?\r"
#define TXT_OPEN        "SHUTTEROPEN?1,1,1,1,1\r"
#define TXT_CLOSE       "SHUTTERCLOSE?1,1,1,1,1\r"
#define TXT_STATUS      "SHUTTERSTATUS?\r"
#define TXT_COOLERON    "COOLERON?100\r"
#define TXT_COOLEROFF   "COOLEROFF?\r"
#define TXT_COOLERT     "COOLERT?\r"
#define TXT_COOLERSTAT  "COOLERSTATUS?\r"
#define TXT_HEATON      "HEATON?100\r"
#define TXT_HEATOFF     "HEATOFF?\r"
#define TXT_HEATSTAT    "HEATSTATUS?\r"
#define TXT_PING        "PING?\r"
#define TXT_ANS_OK      "OK"
#define TXT_ANS_HEATSTAT    "OK\rHEATERSTATUS?"
#define TXT_ANS_COOLERSTAT  "OK\rCOOLERPWM?"
#define TXT_ANS_COOLERT "OK\rCOOLERT?"
#define TXT_ANS_STATUS  "OK\rSHUTTERS?"
#define TXT_ANS_FOCPOS  "OK\rFOCUSERPOS?"

#define FOC_MINPOS  0
#define FOC_MAXPOS  65000
