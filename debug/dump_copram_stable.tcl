# Sample each of the 64 COP RAM nibbles many times; report the most-common value
# and how stable it is (clock digits hold steady, working regs churn).
# Usage: quartus_stp -t debug/dump_copram_stable.tcl
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
set N 24
for {set a 0} {$a < 64} {incr a} {
    write_source_data -instance_index $idx -value $a
    after 2
    array unset cnt
    for {set s 0} {$s < $N} {incr s} {
        set v [read_probe_data -instance_index $idx]
        set hv [format %X [expr 0b$v]]
        if {[info exists cnt($hv)]} { incr cnt($hv) } else { set cnt($hv) 1 }
    }
    # find most common
    set best ""; set bestc 0
    foreach k [array names cnt] { if {$cnt($k) > $bestc} { set bestc $cnt($k); set best $k } }
    set stab [expr {$bestc==$N ? "STABLE" : "churn($bestc/$N)"}]
    if {$best ne "F" || $stab ne "STABLE"} {
        puts "addr [format %2d $a] (0x[format %02X $a]): value=$best  $stab"
    }
}
end_insystem_source_probe
