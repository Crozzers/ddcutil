/* parsed_cmd.h
 *
 * <copyright>
 * Copyright (C) 2014-2016 Sanford Rockowitz <rockowitz@minsoft.com>
 *
 * Licensed under the GNU General Public License Version 2
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * </endcopyright>
 */

#ifndef PARSED_CMD_H_
#define PARSED_CMD_H_

#include <stdbool.h>

#include "base/core.h"
#include "base/displays.h"
#include "base/parms.h"


typedef enum {
   CMDID_NONE         = 0x0000,
   CMDID_DETECT       = 0x0001,
   CMDID_CAPABILITIES = 0x0002,
   CMDID_GETVCP       = 0x0004,
   CMDID_SETVCP       = 0x0008,
   CMDID_LISTVCP      = 0x0010,
   CMDID_TESTCASE     = 0x0020,
   CMDID_LISTTESTS    = 0x0040,
   CMDID_LOADVCP      = 0x0080,
   CMDID_DUMPVCP      = 0x0100,
   CMDID_INTERROGATE  = 0x0200,
   CMDID_ENVIRONMENT  = 0x0400,
   CMDID_USBENV       = 0x0800,
   CMDID_VCPINFO      = 0x1000,
   CMDID_READCHANGES  = 0x2000,
   CMDID_CHKUSBMON    = 0x4000,
   CMDID_PROBE        = 0x8000,
} Cmd_Id_Type;


typedef enum {
   STATS_NONE     = 0x00,
   STATS_TRIES    = 0x01,
   STATS_ERRORS   = 0x02,
   STATS_CALLS    = 0x04,
   STATS_ALL      = 0xFF
} Stats_Type;


#define PARSED_CMD_MARKER  "PCMD"
typedef
struct {
   char                marker[4];      // always PCMD
   Cmd_Id_Type         cmd_id;
   int                 argct;
   char *              args[MAX_ARGS];
   Feature_Set_Ref*    fref;
   Stats_Type          stats_types;
   bool                ddcdata;
#ifdef OLD
   Msg_Level           msg_level;
   bool                programmatic_output;
#endif
   bool                force;
   bool                timestamp_trace;    // prepend trace and debug msgs with elapsed time
   bool                show_unsupported;
   bool                enable_failure_simulation;
   char *              failsim_control_fn;
   Display_Identifier* pdid;
   Trace_Group         trace;
   DDCA_Output_Level   output_level;
   int                 max_tries[3];
   int                 sleep_strategy;
} Parsed_Cmd;

Parsed_Cmd *  new_parsed_cmd();
void          free_parsed_cmd(Parsed_Cmd * parsed_cmd);
void          report_parsed_cmd(Parsed_Cmd * parsed_cmd, int depth);   // debugging function

#endif /* PARSED_CMD_H_ */
