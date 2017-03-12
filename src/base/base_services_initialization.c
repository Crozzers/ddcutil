/* base_services_initialization.c
 *
 * <copyright>
 * Copyright (C) 2014-2017 Sanford Rockowitz <rockowitz@minsoft.com>
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

#include "base/core.h"
#include "base/ddc_packets.h"
#include "base/execution_stats.h"
#include "base/linux_errno.h"
#include "base/sleep.h"

#include "base/base_services_initialization.h"

/** @file base_services_initialization.c
 *
 */

/** Master initialization function for files in subdirectory base
 */
void init_base_services() {
   init_msg_control();
   init_sleep_stats();
   init_execution_stats();
   init_status_code_mgt();
   init_linux_errno();
}
