/* feature_sets.h
 *
 * <copyright>
 * Copyright (C) 2014-2018 Sanford Rockowitz <rockowitz@minsoft.com>
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

/** \file
 *  Feature set identifiers
 */

#ifndef FEATURE_SETS_H_
#define FEATURE_SETS_H_

/** \cond */
#include <stdbool.h>

#include "util/coredefs.h"
/** \endcond */


// If this enum is changed, be sure to change the corresponding
// table in feature_sets.c
typedef enum {
   VCP_SUBSET_PROFILE         = 0x800000,
   VCP_SUBSET_COLOR           = 0x400000,
   VCP_SUBSET_LUT             = 0x200000,
   VCP_SUBSET_CRT             = 0x100000,
   VCP_SUBSET_TV              = 0x080000,
   VCP_SUBSET_AUDIO           = 0x040000,
   VCP_SUBSET_WINDOW          = 0x020000,
   VCP_SUBSET_DPVL            = 0x010000,

   // subsets used only on commands processing,
   // not in feature descriptor table
   VCP_SUBSET_SCAN            = 0x0080,
   VCP_SUBSET_ALL             = 0x0040,
   VCP_SUBSET_SUPPORTED       = 0x0020,
   VCP_SUBSET_KNOWN           = 0x0010,
   VCP_SUBSET_PRESET          = 0x0008,    // uses VCP_SPEC_PRESET
   VCP_SUBSET_MFG             = 0x0004,    // mfg specific codes
   VCP_SUBSET_TABLE           = 0x0002,    // is a table feature
   VCP_SUBSET_SINGLE_FEATURE  = 0x0001,
   VCP_SUBSET_NONE            = 0x0000,
} VCP_Feature_Subset;

char * feature_subset_name(VCP_Feature_Subset subset_id);
char * feature_subset_names(VCP_Feature_Subset subset_ids);

typedef struct {
   VCP_Feature_Subset  subset;
   Byte                specific_feature;
} Feature_Set_Ref;

typedef enum {
   // apply to multiple feature feature sets
   FSF_SHOW_UNSUPPORTED      = 0x01,
   FSF_NOTABLE               = 0x02,

   // applies to single feature feature set
   FSF_FORCE                 = 0x04
} Feature_Set_Flags;

char * feature_set_flag_names(Feature_Set_Flags flags);

void dbgrpt_feature_set_ref(Feature_Set_Ref * fsref, int depth);

#endif /* FEATURE_SETS_H_ */
