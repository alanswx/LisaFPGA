# Freeze the COP (LCRM source bit 6) so its RAM is static, then dump all 64
# nibbles cleanly. Unfreezes at the end. Usage: quartus_stp -t debug/dump_copram_frozen.tcl
package require ::quartus::insystem_source_probe
set usb [lindex [get_hardware_names] 0]
set dev ""
foreach dd [get_device_names -hardware_name $usb] {
    if {[string match -nocase "*5CSE*" $dd]} { set dev $dd }
}
set insts [get_insystem_source_probe_instance_info -hardware_name $usb -device_name $dev]
set idx -1
foreach inst $insts { if {[lindex $inst 3] eq "LCRM"} { set idx [lindex $inst 0] } }
if {$idx < 0} { puts "LCRM not found"; exit 1 }
start_insystem_source_probe -hardware_name $usb -device_name $dev
set FREEZE 64
set line ""
for {set a 0} {$a < 64} {incr a} {
    write_source_data -instance_index $idx -value [expr {$a | $FREEZE}]
    after 5
    set v [read_probe_data -instance_index $idx]
    append line "[format %X [expr 0b$v]] "
    if {($a % 16) == 15} { puts "addr [format %02d [expr $a-15]]-[format %02d $a]: $line"; set line "" }
}
# unfreeze
write_source_data -instance_index $idx -value 0
end_insystem_source_probe
puts "(COP unfrozen)"
