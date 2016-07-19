/* hid_report_descriptor.c
 *
 * Interpret a HID Report Descriptor
 *
 * <copyright>
 * Copyright (C) 2014-2015 Sanford Rockowitz <rockowitz@minsoft.com>
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

#include <assert.h>
#include <errno.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <wchar.h>

#include "util/string_util.h"
#include "util/report_util.h"
#include "util/device_id_util.h"


#include "util/hid_report_descriptor.h"


//
// Lookup tables
//

static const char* report_type_name_table[] = {
      "invalid",
      "Input",
      "Output",
      "Feature"
};


/* Returns a string representation of a report type id
 *
 * Arguments:  report_type
 *
 * Returns:  string representation of id
 */
const char * hid_report_type_name(Byte report_type) {
   if (report_type < HID_REPORT_TYPE_MIN || report_type > HID_REPORT_TYPE_MAX)
      report_type = 0;
   return report_type_name_table[report_type];
}

static const char* collection_type_name_table[] = {
      "Physical",
      "Application",
      "Logical",
      "Report",
      "Named Array",
      "Usage Switch",
      "Usage Modifier"
};

/* Returns a string representation of a collection type byte.
 */
const char * collection_type_name(Byte collection_type) {
   if (collection_type >= sizeof(collection_type_name_table))
      return "invalid";
   else
      return collection_type_name_table[collection_type];
}


/* Create a string representation of Main Item flags bitfield.
 * The representation is returned in a buffer provided, which
 * must be at least 150 bytes in size.
 *
 * Arguments:
 *    data       flags
 *    buffer     where to save formatted response
 *    bufsz      buffer size
 *
 * Returns:      buffer
 */
char * interpret_item_flags_r(uint16_t data, char * buffer, int bufsz) {
   assert(buffer && bufsz > 150);

   snprintf(buffer, bufsz, "%s %s %s %s %s %s %s %s %s",
       data &  0x01 ? "Constant"           : "Data",
       data &  0x02 ? "Variable"           : "Array",
       data &  0x04 ? "Relative"           : "Absolute",
       data &  0x08 ? "Wrap"               : "No_Wrap",
       data &  0x10 ? "Non_Linear"         : "Linear",
       data &  0x20 ? "No_Preferred_State" : "Preferred_State",
       data &  0x40 ? "Null_State"         : "No_Null_Position",
       data &  0x80 ? "Volatile"           : "Non_Volatile",
       data & 0x100 ? "Buffered Bytes"     : "Bitfield");
   return buffer;
}


//
// Functions to report Parsed_Hid_Descriptor and its contained structs
//

void report_hid_field(Hid_Field * hf, int depth) {
   int d1 = depth+1;
   // rpt_structure_loc("Hid_Field", hf, depth);
   rpt_title("Field: ", depth);
   char buf[200];
   rpt_vstring(d1, "%-20s:  0x%04x      %s", "Usage page",
                   hf->usage_page,
                   devid_usage_code_page_name(hf->usage_page));

   // deprecated
   // rpt_vstring(d1, "%-20s:  0x%04x  %s", "Usage id",
   //                 hf->usage_id,
   //                 devid_usage_code_id_name(hf->usage_page, hf->usage_id));

   rpt_vstring(d1, "%-20s:  0x%08x  %s", "Extended Usage",
                   hf->extended_usage,
                   devid_usage_code_name_by_extended_id(hf->extended_usage));

   rpt_vstring(d1, "%-20s:  0x%04x      %s", "Item flags",
                   hf->item_flags,
                   interpret_item_flags_r(hf->item_flags, buf, 200) );
   rpt_vstring(d1, "%-20s:  0x%04x      %d", "Logical minimum",
                   hf->logical_minimum, hf->logical_minimum);
   rpt_vstring(d1, "%-20s:  0x%04x      %d", "Logical maximum",
                   hf->logical_maximum, hf->logical_maximum);
   rpt_vstring(d1, "%-20s:  0x%04x      %d", "Physical minimum",
                   hf->physical_minimum, hf->physical_minimum);
   rpt_vstring(d1, "%-20s:  0x%04x      %d", "Logical maximum",
                   hf->physical_maximum, hf->physical_maximum);
   rpt_vstring(d1, "%-20s:  %d", "Report size", hf->report_size);
   rpt_vstring(d1, "%-20s:  %d", "Report count", hf->report_count);
   rpt_vstring(d1, "%-20s:  0x%04x      %d", "Unit_exponent", hf->unit_exponent, hf->unit_exponent);
   rpt_vstring(d1, "%-20s:  0x%04x      %d", "Unit", hf->unit, hf->unit);

}


void report_hid_report(Hid_Report * hr, int depth) {
   int d1 = depth+1;
   // int d2 = depth+2;
   // rpt_structure_loc("Hid_Report", hr,depth);
   rpt_vstring(depth, "%-20s:%*s 0x%02x  %d", "Report id",   rpt_indent(1), "", hr->report_id, hr->report_id);
   rpt_vstring(d1, "%-20s: 0x%02x  %s", "Report type",
                   hr->report_type, hid_report_type_name(hr->report_type) );

#ifdef OLD
   if (hr->hid_field_list) {
      rpt_title("Fields: ", d1);
      Hid_Field * cur = hr->hid_field_list;
      while (cur) {
         report_hid_field(cur,  d2);
         cur = cur->next;

      }
   }
   else
      rpt_vstring(d1, "%-20s: none", "Fields");
#endif
   //alt
   if (hr->hid_fields && hr->hid_fields->len > 0) {
      // rpt_title("Fields: (alt) ", d1);
      for (int ndx=0; ndx<hr->hid_fields->len; ndx++) {
         report_hid_field( g_ptr_array_index(hr->hid_fields, ndx), d1);
      }
   }
   else
      rpt_vstring(d1, "%-20s: none   (alt)", "Fields");
}


void report_hid_collection(Hid_Collection * col, int depth) {
   int d1 = depth+1;
   rpt_structure_loc("Hid_Collection", col, depth);
   if (col->is_root_collection)
      rpt_title("Dummy root collection", d1);
   else {
      rpt_vstring(d1, "%-20s:  x%02x  %s", "Collection type",
                      col->collection_type, collection_type_name(col->collection_type));
      rpt_vstring(d1, "%-20s:  x%02x  %s", "Usage page",
                      col->usage_page, devid_usage_code_page_name(col->usage_page));
      rpt_vstring(d1, "%-20s:  x%02x  %s", "Usage id",
                      col->usage_id, devid_usage_code_id_name(col->usage_page, col->usage_id));
   }

   if (col->child_collections && col->child_collections->len > 0) {
      rpt_title("Contained collections: ", d1);
      for (int ndx = 0; ndx < col->child_collections->len; ndx++) {
         Hid_Collection * a_child = g_ptr_array_index(col->child_collections, ndx);
         report_hid_collection(a_child, d1);
      }
   }

   if (col->reports && col->reports->len > 0) {
      rpt_title("Reports:", d1);
      for (int ndx = 0; ndx < col->reports->len; ndx++)
         report_hid_report(g_ptr_array_index(col->reports, ndx), d1);
   }
   else
      rpt_vstring(d1, "%-20s:  None", "Reports");
}


void report_parsed_hid_descriptor(Parsed_Hid_Descriptor * pdesc, int depth) {
   int d1 = depth + 1;
   rpt_structure_loc("Parsed_Hid_Descriptor", pdesc, depth);
   report_hid_collection(pdesc->root_collection, d1);
}


//
// Data structures and functions For building Parsed_Hid_Descriptor
//

struct cur_report_globals;

typedef
struct cur_report_globals {
   uint16_t  usage_page;
   int16_t   logical_minimum;
   int16_t   logical_maximum;
   int16_t   physical_minimum;
   int16_t   physical_maximum;
   uint16_t  unit_exponent;
   uint16_t  unit;
   uint16_t  report_size;
   uint16_t  report_id;
   uint16_t  report_count;   // number of data fields for the item
   struct cur_report_globals *  prev;
} Cur_Report_Globals;


#ifdef OLD
struct local_item_value;
typedef
struct local_item_value {
   uint16_t  value;
   struct local_item_value * next;
} Local_Item_Value;
#endif

typedef
struct cur_report_locals {
#ifdef OLD
   Local_Item_Value * usage;
#endif
   // if bSize = 3, usages are 4 byte extended usages
   int         bSize;    // actually just 0..4
   GArray *    usages;
   uint32_t    usage_minimum;
   uint32_t    usage_maximum;
#ifdef OLD
   Local_Item_Value *    designator_index;      // TO ELIMINATE
#endif
   GArray *    designator_indexes;
   uint16_t    designator_minimum;
   uint16_t    designator_maximum;
#ifdef OLD
   Local_Item_Value *    string_index;          // TO_ELIMINATE
#endif
   GArray *    string_indexes;
   uint16_t    string_maximum;
   uint16_t    string_minimum;
} Cur_Report_Locals;


#ifdef OLD
void free_local_item_value_chain(Local_Item_Value * lav) {
   // TODO
}
#endif

void free_cur_report_locals(Cur_Report_Locals * locals) {
   if (locals) {
#ifdef OLD
      free(locals->usage);
      free(locals->string_index);
      free(locals->designator_index);
#endif
      if (locals->usages)
         g_array_free(locals->usages, true);
      if (locals->string_indexes)
         g_array_free(locals->string_indexes, true);
      if (locals->designator_indexes)
         g_array_free(locals->designator_indexes, true);
      free(locals);
   }
}



Hid_Report * find_hid_report(Hid_Collection * col, Byte report_type, uint16_t report_id) {
   Hid_Report * result = NULL;

   if (col->reports->len) {
      for (int ndx=0; ndx < col->reports->len && !result; ndx++) {
         Hid_Report * cur = g_ptr_array_index(col->reports, ndx);
         if (cur->report_type == report_type && cur->report_id == report_id)
            result = cur;
      }
   }

   return result;
}

Hid_Report * find_hid_report_or_new(Hid_Collection * hc, Byte report_type, uint16_t report_id) {
   bool debug = false;
   if (debug)
      printf("(%s) report_type=%d, report_id=%d\n", __func__, report_type, report_id);
   Hid_Report * result = find_hid_report(hc, report_type, report_id);
   if (!result) {
      if (!hc->reports) {
         hc->reports = g_ptr_array_new();
      }
      result = calloc(1, sizeof(Hid_Report));
      result->report_id = report_id;
      result->report_type = report_type;
      g_ptr_array_add(hc->reports, result);
   }
   return result;
}

void add_report_field(Hid_Report * hr, Hid_Field * hf) {
   assert(hr && hf);
#ifdef OLD
   if (!hr->hid_field_list)
      hr->hid_field_list = hf;
   else {
      Hid_Field * cf = hr->hid_field_list;
      while (cf->next)
         cf = cf->next;
      cf->next = hf;
   }
#endif
   // new way:
   if (!hr->hid_fields)
      hr->hid_fields = g_ptr_array_new();
   g_ptr_array_add(hr->hid_fields, hf);
}



void add_hid_collection_child(Hid_Collection * parent, Hid_Collection * new_child) {
   if (!parent->child_collections)
      parent->child_collections = g_ptr_array_new();

   g_ptr_array_add(parent->child_collections, new_child);
}


/* From the Device Class Definition for Human Interface Devices:

Interpretation of Usage, Usage Minimum orUsage Maximum items vary as a
function of the item’s bSize field. If the bSize field = 3 then the item is
interpreted as a 32 bit unsigned value where the high order 16 bits defines the
Usage Page  and the low order 16 bits defines the Usage ID. 32 bit usage items
that define both the Usage Page and Usage ID are often referred to as
“Extended” Usages.

If the bSize field = 1 or 2 then the Usage is interpreted as an unsigned value
that selects a Usage ID on the currently defined Usage Page. When the parser
encounters a main item it concatenates the last declared Usage Page with a
Usage to form a complete usage value. Extended usages can be used to
override the currently defined Usage Page for individual usages.
 */



uint32_t extended_usage(uint16_t usage_page, uint32_t usage, int usage_bsize) {
   bool debug = false;
   uint32_t result = 0;
   if (usage_bsize == 3 || usage_bsize == 4)
      result = usage;
   else if (usage_bsize == 1 || usage_bsize == 2) {
      assert( (usage & 0xff00) == 0);
      result = usage_page <<16 | usage;
   }
   else if (usage & 0xff00)
      result = usage;
   else
      result = usage_page << 16 | usage;

   if (debug) {
      printf("(%s) usage_page=0x%04x, usage=0x%08x, usage_bsize=%d, returning 0x%08x\n",
             __func__, usage_page, usage, usage_bsize, result);
   }
   return result;
}




Parsed_Hid_Descriptor *
parse_report_desc(Byte * b, int desclen) {
   bool debug = false;
   if (debug)
      printf("(%s) Starting. b=%p, desclen=%d\n", __func__, b, desclen);

   char *types[4] = { "Main", "Global", "Local", "reserved" };

   unsigned int j, bsize, btag, btype, data = 0xffff;
   // unsigned int hut = 0xffff;
   int i;
   // char indent[] = "                            ";

   Cur_Report_Globals * cur_globals = calloc(1, sizeof(struct cur_report_globals));
   Cur_Report_Locals  * cur_locals  = calloc(1, sizeof(struct cur_report_locals));
   // Hid_Report * cur_report;
   Hid_Collection * cur_collection = NULL;
   // Hid_Collection * collection_list = NULL;


   Parsed_Hid_Descriptor * parsed_descriptor = calloc(1, sizeof(Parsed_Hid_Descriptor));
   parsed_descriptor->root_collection = calloc(1,sizeof(Hid_Collection));
   parsed_descriptor->root_collection->is_root_collection = true;

#define COLLECTION_STACK_SIZE 10
   Hid_Collection * collection_stack[COLLECTION_STACK_SIZE];
   collection_stack[0] = parsed_descriptor->root_collection;
   int collection_stack_cur = 0;


   for (i = 0; i < desclen; ) {
      bsize = b[i] & 0x03;           // first 2 bits are size indicator
      if (bsize == 3)                // values are indicators, not the actual size:
         bsize = 4;                  //  0,1,2,4
      btype = b[i] & (0x03 << 2);    // next 2 bits are type
      btag = b[i] & ~0x03;           // mask out size bits to get tag

      //   printf("             Item(%-6s): %s, data=", types[btype>>2],
      //       names_reporttag(btag));                                       // ok

      data = 0;
      if (bsize > 0) {
         // printf(" [ ");
         for (j = 0; j < bsize; j++) {
            // if (debug)
            //    printf("0x%02x ", b[i+1+j]);
            data += (b[i+1+j] << (8*j));
         }
      }   // bsize > 0

      if (debug) {
         char datastr[20];
         switch(bsize) {
         case 0:
            strcpy(datastr, "none");
            break;
         case 1:
            snprintf(datastr, 20, "[0x%02x] %d", data, data);
            break;
         case 2:
            snprintf(datastr, 20, "[0x%04x] %d", data, data);
            break;
         case 4:
            snprintf(datastr, 20, "[0x%08x] %d", data, data);
            break;
         default:
           // program logic error
            break;
         }
         printf("(%s) Item(%-6s): %s, data=%s\n",
                 __func__,
                 types[btype>>2],
                 devid_hid_descriptor_item_type(btag),
                 datastr);
      }


      switch (btype) {

      // Main item tags

      case 0x00:     // Main item
         switch(btag) {

         case 0xa0:     // Collection
         {
            cur_collection = calloc(1, sizeof(Hid_Collection));
            cur_collection->collection_type = data;
            cur_collection->usage_page = cur_globals->usage_page;
            // cur_collection->usage_id   = cur_locals->usage->value;    // TODO: ensure value exists
            if (cur_locals->usages && cur_locals->usages->len > 0) {
               cur_collection->usage_id = g_array_index(cur_locals->usages, uint32_t, 0);
            }
            else {
               printf("(%s) No usage id has been set for collection\n", __func__);
            }
            cur_collection->extended_usage = extended_usage(
                                                cur_globals->usage_page,
                                                cur_collection->usage_id,
                                                cur_locals->bSize);
                                       //         0 /* use heuristic */);

            cur_collection->reports = g_ptr_array_new();

            // add this collection as a child of the parent collection
            add_hid_collection_child(collection_stack[collection_stack_cur], cur_collection);
            assert(collection_stack_cur < COLLECTION_STACK_SIZE-1);
            collection_stack[++collection_stack_cur] = cur_collection;
            break;
         }

         case 0x80: /* Input */
         case 0x90: /* Output */
         case 0xb0: /* Feature */
         {
            Hid_Field * hf = calloc(1, sizeof(Hid_Field));
            Byte report_type;
            if      (btag == 0x80) report_type = HID_REPORT_TYPE_INPUT;
            else if (btag == 0x90) report_type = HID_REPORT_TYPE_OUTPUT;
            else                   report_type = HID_REPORT_TYPE_FEATURE;
            hf->item_flags = data;
            uint16_t report_id = cur_globals->report_id;
            Hid_Report * hr = find_hid_report_or_new(
                     cur_collection,
                     report_type,
                     report_id);

            // add this item/field to current report
            add_report_field(hr, hf);

            int field_index = hr->hid_fields->len - 1;
            if (cur_locals->usages && cur_locals->usages->len > 0) {
               int usagect = cur_locals->usages->len;
               int usagendx = (field_index < usagect) ? field_index : usagect-1;
               uint32_t this_usage = g_array_index(cur_locals->usages, uint32_t, usagendx);
               // hf->usage_id = this_usage;
               hf->extended_usage = extended_usage(cur_globals->usage_page,
                                                   this_usage,
                                                   0 /* use heuristic */);
               if (debug) {
                  printf("(%s) item 0x%02x, usagect=%d, usagendx=%d, this_usage=0x%04x\n", __func__,
                         btag, usagect, usagendx, this_usage);
               }
            }
            else {
               printf("(%s) Tag 0x%02x, Report id: %d: No usage values in cur_locals\n",
                     __func__, btag, report_id);
            }

            hf->usage_page       = cur_globals->usage_page;
            hf->logical_minimum  = cur_globals->logical_minimum;
            hf->logical_maximum  = cur_globals->logical_maximum;
            hf->physical_minimum = cur_globals->physical_minimum;
            hf->physical_maximum = cur_globals->physical_maximum;
            hf->report_size      = cur_globals->report_size;
            hf->report_count     = cur_globals->report_count;
            hf->unit_exponent    = cur_globals->unit_exponent;
            hf->unit             = cur_globals->unit;

#define UNHANDLED(F) \
   if (cur_locals->F) \
      printf("%s) Tag 0x%02x, Unimplemented: %s\n", __func__, btag, #F);

            UNHANDLED(designator_indexes)
            UNHANDLED(designator_minimum)
            UNHANDLED(designator_maximum)
            UNHANDLED(string_indexes)
            UNHANDLED(string_minimum)
            UNHANDLED(string_maximum)
            UNHANDLED(usage_minimum)
#undef UNHANDLED

            break;
         }
         case 0xc0: // End Collection
            if (collection_stack_cur == 0) {
               printf("(%s) End Collection item without corresponding Collection\n", __func__);
            }
            collection_stack_cur--;
            break;
         default:
            break;
         }   // switch(btag)

         free_cur_report_locals(cur_locals);
         cur_locals  = calloc(1, sizeof(struct cur_report_locals));
         break;

      // Global item tags

      case 0x04:     // Global item
         switch (btag) {
         case 0x04: /* Usage Page */
              cur_globals->usage_page = data;
              break;
         case 0x14:       // Logical Minimum
              cur_globals->logical_minimum = data;
              break;
         case 0x24:
              cur_globals->logical_maximum = data;
              break;
         case 0x34:
              cur_globals->physical_minimum = data;
              break;
         case 0x44:
              cur_globals->physical_maximum = data;
              break;
         case 0x54:     // Unit Exponent
              cur_globals->unit_exponent = data;                     // Global
              break;
         case 0x64:     // Unit
              cur_globals->unit = data;      // ??                   // Global
              break;
         case 0x74:
              cur_globals->report_size = data;
              break;
         case 0x84:
              cur_globals->report_id = data;
              break;
         case 0x94:
              cur_globals->report_count = data;
              break;
         case 0xa4:      // Push
         {
              Cur_Report_Globals* old_globals = cur_globals;
              cur_globals = calloc(1, sizeof(Cur_Report_Globals));
              cur_globals->prev = old_globals;
              break;
         }
         case 0xb4:     // Pop
              if (!cur_globals->prev) {
                 printf("(%s) Invalid item Pop without previous Push\n", __func__);
              }
              else {
                 Cur_Report_Globals * popped_globals = cur_globals;
                 cur_globals = cur_globals->prev;
                 free(popped_globals);
              }
              break;
         default:
              printf("(%s) Invalid global item tag: 0x%02x\n", __func__, btag);

         }   // switch(btag)
         break;


      // Local item tags

      case 0x08:     // Local item
         switch(btag) {
         case 0x08:     // Usage
           {
              if (debug)
                 printf("(%s) tag 0x08 (Usage), bSize=%d, value=0x%08x %d\n", __func__, bsize, data, data);
#ifdef OLD
              Local_Item_Value * lav = calloc(1, sizeof(Local_Item_Value));
              lav->value = data;
              if (cur_locals->usage == NULL)
                 cur_locals->usage = lav;
              else {
                 Local_Item_Value * v = cur_locals->usage;
                 while (v->next)
                    v = v->next;
                 v->next = lav;
              }
#endif
              // alt:

              if (cur_locals->usages == NULL)
                 cur_locals->usages = g_array_new(
                       /* null terminated */ false,
                       /* init to 0       */ true,
                       /* field size      */ sizeof(uint32_t) );
              g_array_append_val(cur_locals->usages, data);
              if (cur_locals->usages->len > 1) {
                 printf("(%s) After append, cur_locals->usages->len = %d\n", __func__,
                        cur_locals->usages->len);
              }
              if (cur_locals->usages->len == 1)
                 cur_locals->bSize = bsize;
              else {
                 if (bsize != cur_locals->bSize &&
                       cur_locals->bSize != 0)       // avoid redundant messages
                 {
                    printf("(%s) Warning: Multiple usages for fields have different size values\n", __func__);
                    printf("     Switching to heurisitic interpretation of usage\n");
                    cur_locals->bSize = 0;
                 }
              }
              break;
           }
           case 0x18:     // Usage minimum
             cur_locals->usage_minimum = data;
             break;
           case 0x28:
              cur_locals->usage_maximum = data;
              break;
           case 0x38:    // designator index
              // TODO: same as 0x08 Usage
              printf("(%s) Local item value 0x38 (Designator Index) unimplemented\n", __func__);
              break;
           case 0x48:
              cur_locals->designator_minimum = data;
              break;
           case 0x58:
              cur_locals->designator_maximum = data;
              break;
           case 0x78:           // string index
              // TODO: same as 0x08 Usage
              printf("(%s) Local item value 0x78 (String Index) unimplemented\n", __func__);
              break;
           case 0x88:
              cur_locals->string_minimum = data;
              break;
           case 0x98:
              cur_locals->string_maximum = data;
              break;
           case 0xa8:     // delimiter - defines beginning or end of set of local items
              // what to do?
              printf("(%s) Local item Delimiter unimplemented\n", __func__);
              break;
           default:
              printf("(%s) Invalid local item tax: 0x%02x\n", __func__, btag);
         }
         break;

      default:
           printf("(%s) Invalid item type: 0x%04x\n", __func__, btype);

      }


      i += 1 + bsize;
   }

   return parsed_descriptor;
}

