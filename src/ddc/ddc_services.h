/* ddc_services.h
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

#ifndef DDC_SERVICES_H_
#define DDC_SERVICES_H_

#include <stdio.h>

void init_ddc_services();

void ddc_reset_ddc_stats();
void ddc_reset_all_stats();

void ddc_reset_all_stats();
void ddc_report_all_stats(Stats_Type stats, int depth);

void ddc_report_max_tries(int depth);

#endif /* DDC_SERVICES_H_ */
