import sys
# s.path.insert(0,"/usr/local/lib64/python2.7/site-packages/")
# print "sys.path:", sys.pat#
import ddc_swig
# print dir(ddc_swig)
from ddc_swig import *

def test1():
   print "Default output level: %d" % ddca_get_output_level()
   ddca_set_output_level(OL_VERBOSE);
   print "Output level reset to %s" %ddca_output_level_name(ddca_get_output_level())
   
   print "Initial report ddc errors setting: %s" % ddca_is_report_ddc_errors_enabled()
   ddca_enable_report_ddc_errors(True)
 
   displayct = ddcs_report_active_displays(2)
   print "Found %d active displays" % displayct 

   did = ddcs_create_dispno_display_identifier(2)
   print ddcs_repr_display_identifier(did)
   dref = ddcs_get_display_ref(did)
   print ddcs_repr_display_ref(dref)
   ddcs_report_display_ref(dref, 2)
   dh = ddcs_open_display(dref)
   print ddcs_repr_display_handle(dh)

   print "Name of feature 0x10: %s" % ddcs_get_feature_name(0x10)
   # feature_flags = ddcs_get_feature_info_by_display(dh, 0x10)
   # print "Feature 0x10 flags: %s, %d, 0x%08x" % (feature_flags, feature_flags, feature_flags)

   vcp_val = ddcs_get_nontable_vcp_value(dh, 0x10)
   print vcp_val
   print "cur value: ", vcp_val.cur_value
   print "max value: ", vcp_val.max_value
   ddcs_set_nontable_vcp_value(dh, 0x10, 22)
   print "value reset to", ddcs_get_nontable_vcp_value(dh, 0x10).cur_value

   ddcs_set_nontable_vcp_value(dh, 0x10, vcp_val.cur_value)
   print "value reset to", ddcs_get_nontable_vcp_value(dh, 0x10).cur_value

   caps  = ddcs_get_capabilities_string(dh)
   print "Capabilities: %s" % caps

   profile_vals = ddcs_get_profile_related_values(dh)
   print "Profile related values: %s" % profile_vals 


   # print vcp_val.cur_value()     # int object is not callable
   # ddcs_close_display(dh)

print "(test_swig)"
print dir(ddc_swig)
test1()

