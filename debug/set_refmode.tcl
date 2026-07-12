# Set the SDRAM refresh policy (LRAM source[0]): 0 = legacy burst, 1 = slot-safe.
# Usage: quartus_stp -t debug/set_refmode.tcl <0|1>
package require ::quartus::insystem_source_probe
set d [expr {[lindex $quartus(args) 0] & 1}]
set usb [lindex [get_hardware_names] 0]
set dev ""
foreach dd [get_device_names -hardware_name $usb] {
    if {[string match -nocase "*5CSE*" $dd]} { set dev $dd }
}
set insts [get_insystem_source_probe_instance_info -hardware_name $usb -device_name $dev]
set idx -1
foreach inst $insts {
    if {[lindex $inst 3] eq "LRAM"} { set idx [lindex $inst 0] }
}
if {$idx < 0} { puts "LRAM instance not found"; exit 1 }
start_insystem_source_probe -hardware_name $usb -device_name $dev
write_source_data -instance_index $idx -value $d -value_in_hex
end_insystem_source_probe
puts "ref_mode set to $d ([expr {$d ? {slot-safe} : {legacy burst}}])"
