# Dump all 64 nibbles of the COP data RAM via the LCRM ISSP (source=addr, probe=nibble).
# Usage: quartus_stp -t debug/dump_copram.tcl
package require ::quartus::insystem_source_probe
set usb [lindex [get_hardware_names] 0]
set dev ""
foreach dd [get_device_names -hardware_name $usb] {
    if {[string match -nocase "*5CSE*" $dd]} { set dev $dd }
}
set insts [get_insystem_source_probe_instance_info -hardware_name $usb -device_name $dev]
set idx -1
foreach inst $insts { if {[lindex $inst 3] eq "LCRM"} { set idx [lindex $inst 0] } }
if {$idx < 0} { puts "LCRM instance not found"; exit 1 }
start_insystem_source_probe -hardware_name $usb -device_name $dev
set line ""
for {set a 0} {$a < 64} {incr a} {
    write_source_data -instance_index $idx -value $a
    after 5
    set v [read_probe_data -instance_index $idx]
    # v is a binary string (4 bits); convert to hex
    set hv [format %X [expr 0b$v]]
    append line "$hv "
    if {($a % 16) == 15} { puts "addr [format %02d [expr $a-15]]-[format %02d $a]: $line"; set line "" }
}
end_insystem_source_probe
