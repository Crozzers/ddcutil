/* x11_util.h
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

#ifndef X11_UTIL_H_
#define X11_UTIL_H_

#include <glib.h>

#include "util/coredefs.h"


typedef struct {
   char * output_name;
   Byte * edidbytes;
} X11_Edid_Rec;


GPtrArray * get_x11_edids();   // returns array of X11_Edid_Rec

void free_x11_edids(GPtrArray * edidrecs);

#endif /* X11_UTIL_H_ */
