# Peek an SDRAM word via the repurposed LRAM ISSP port.
# Usage: quartus_stp -t debug/peek_ram.tcl <physical_byte_addr_hex> [count]
#   e.g. quartus_stp -t debug/peek_ram.tcl 800        -> reads physical 0x800
#        quartus_stp -t debug/peek_ram.tcl 800 8       -> 8 consecutive words
# ram_dbg = { peek_data[63:48], peek_addr_q[47:24], peek_cnt[23:8], 0[7:2], st_state[1:0] }
package require ::quartus::insystem_source_probe

set byte_addr [expr {"0x[lindex $quartus(args) 0]"}]
set count [lindex $quartus(args) 1]
if {$count eq ""} { set count 1 }

set usb [lindex [get_hardware_names] 0]
set dev ""
foreach d [get_device_names -hardware_name $usb] {
    if {[string match -nocase "*5CSE*" $d]} { set dev $d }
}
set insts [get_insystem_source_probe_instance_info -hardware_name $usb -device_name $dev]
set idx -1
foreach inst $insts {
    if {[lindex $inst 3] eq "LRAM"} { set idx [lindex $inst 0] }
}
if {$idx < 0} { puts "LRAM instance not found"; exit 1 }

start_insystem_source_probe -hardware_name $usb -device_name $dev

for {set i 0} {$i < $count} {incr i} {
    set ba [expr {$byte_addr + $i*2}]
    set word_addr [expr {$ba >> 1}]
    write_source_data -instance_index $idx -value [format %06X $word_addr] -value_in_hex
    # let the peek FSM issue the read at the new address (several loops)
    after 50
    set raw [read_probe_data -instance_index $idx -value_in_hex]
    # raw is 16 hex chars (64 bits). Parse fields.
    set val   [string range $raw 0 3]
    set aecho [string range $raw 4 9]
    set cnt   [string range $raw 10 13]
    puts [format "phys 0x%06X (word 0x%06X): data=0x%s  addr_echo=0x%s  cnt=0x%s" $ba $word_addr $val $aecho $cnt]
}
end_insystem_source_probe
