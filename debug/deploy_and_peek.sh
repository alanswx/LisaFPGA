#!/bin/bash
# Deploy the freshly-built probed rbf, launch via the ProFile MGL, wait for boot
# to the STARTUP menu, then peek the MMU utility at physical 0x800 (word 0x400)
# and the scatter target 0x1600 (word 0xB00). Expected: physical 0x800 -> 0x2A38.
set -e
STP=/home/alans/intelFPGA_lite/quartus/bin/quartus_stp
cd /home/alans/mister/LisaFPGA

echo "=== deploying rbf ($(ls -la --time-style='+%H:%M' output_files/Lisa.rbf | awk '{print $6}')) ==="
sshpass -p 1 scp -o StrictHostKeyChecking=no output_files/Lisa.rbf root@192.168.1.196:/media/fat/Lisa.rbf
echo "=== launching via lisa_profile.mgl ==="
curl -s -X POST http://192.168.1.196:8182/api/launch -H "Content-Type: application/json" \
     -d '{"path":"/media/fat/lisa_profile.mgl"}' >/dev/null
echo "waiting 18s for boot to menu..."
sleep 18

echo "=== sanity: peek a small span from 0 and from 0x800 (liveness: cnt should advance, addr_echo match) ==="
$STP -t debug/peek_ram.tcl 0 4 2>/dev/null | grep -aE "phys|not found"
echo "--- MMU utility region (expect 0x2A38 at 0x800) ---"
$STP -t debug/peek_ram.tcl 800 8 2>/dev/null | grep -aE "phys|not found"
echo "--- scatter target 0x1600 (0x2A38 here => MMU scattered the store) ---"
$STP -t debug/peek_ram.tcl 1600 8 2>/dev/null | grep -aE "phys|not found"
