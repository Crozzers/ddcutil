/* base_hid_report_descriptor.h
 *
 * Functions to perform basic parsing of the HID Report Descriptor and
 * display the contents of the Report Descriptor in the format used
 * in HID documentation.
 *
 * <copyright>
 * Copyright (C) 2016 Sanford Rockowitz <rockowitz@minsoft.com>
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

#ifndef BASE_HID_REPORT_DESCRIPTOR_H_
#define BASE_HID_REPORT_DESCRIPTOR_H_

#include <stdint.h>

#include "util/coredefs.h"

typedef struct hid_report_item {
   struct hid_report_item * next;

   Byte     btype;         // Main, Global, Local
   Byte     btag;
   Byte     bsize;
   uint32_t data;
} Hid_Report_Item;

void report_hid_report_item_list(Hid_Report_Item * head, int depth);
void free_hid_report_item_list(Hid_Report_Item * head);
Hid_Report_Item * preparse_hid_report(Byte * b, int l) ;

#endif /* BASE_HID_REPORT_DESCRIPTOR_H_ */
