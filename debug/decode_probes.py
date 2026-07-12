#!/usr/bin/env python3
"""Decode LDBG/LVID/LRAM/LCPU/LBUS ISSP probe hex dumps from read_probes.tcl output (stdin)."""
import sys, re

def bits(v, hi, lo=None):
    if lo is None: lo = hi
    return (v >> lo) & ((1 << (hi - lo + 1)) - 1)

for line in sys.stdin:
    m = re.match(r'\s*(L\w+)\[\d+\]\s*=\s*0x([0-9A-Fa-f]+)', line)
    if not m:
        continue
    name, v = m.group(1), int(m.group(2), 16)
    print(f"== {name} = 0x{m.group(2)}")
    if name == 'LDBG':
        print(f"  vsync_cnt={bits(v,31,24)} hsync_cnt={bits(v,23,16)} dotck_div={bits(v,15,8)}")
        print(f"  pll_locked={bits(v,7)} ON={bits(v,6)} ON_sync={bits(v,5)} reset_n={bits(v,4)}")
        print(f"  spd_ovr_en={bits(v,3)} force_on={bits(v,2)} spd_ovr={bits(v,1,0)}")
    elif name == 'LVID':
        print(f"  first_w={bits(v,31,20)} (want 720)  n_lines={bits(v,19,10)} first_de_pos={bits(v,9,0)}")
    elif name == 'LVI2':
        print(f"  vid_l={bits(v,31,21)} vid_r={bits(v,20,10)} (content edges in hcnt) per_min={bits(v,9,0)}")

    elif name == 'LMOU':
        print(f"  pkt_cnt={bits(v,63,48)} rep_cnt={bits(v,47,32)} dx=0x{bits(v,31,24):02X} dy=0x{bits(v,23,16):02X} flags=0x{bits(v,15,8):02X} pend={bits(v,7)}")
    elif name == 'LRAM':
        # bridge-race observer: { max_latency[63:48], overlap_cnt[47:24], wr_cnt[23:12], rd_cnt[11:0] }
        print(f"  overlap_cnt=0x{bits(v,47,24):06X} (want 0)  max_latency={bits(v,63,48)} cyc (budget 32@1x)")
        print(f"  wr_cnt=0x{bits(v,23,12):03X} rd_cnt=0x{bits(v,11,0):03X}")
    elif name == 'LCPU':
        pc = bits(v,55,33) << 1
        print(f"  haltn={bits(v,63)} rstoutn={bits(v,62)} berr_cnt={bits(v,61,56)} PC~0x{pc:06X} asn={bits(v,32)}")
        print(f"  IPL_n={bits(v,31,29):03b} ipl7_seen={bits(v,28)}")
        print(f"  HDERint={bits(v,27)} seen={bits(v,26)}  _HDERin={bits(v,25)} in_seen={bits(v,24)}  _HDERlat={bits(v,23)} lat_seen={bits(v,22)}  _HDMSK={bits(v,21)}")
        print(f"  _HPIR={bits(v,20)} hpir_seen={bits(v,19)}")
        print(f"  SFERint={bits(v,18)} seen={bits(v,17)}  _SFERin={bits(v,16)} in_seen={bits(v,15)}  _SFERlat={bits(v,14)} lat_seen={bits(v,13)}  _SFMSK={bits(v,12)} nmi_seen={bits(v,11)}")
        print(f"  vpawr@Efall: _DBON={bits(v,10)} _SPIO={bits(v,9)} _AS={bits(v,8)} UDlo_nz={bits(v,7)} UDhi_nz={bits(v,6)} ud_nz_ever={bits(v,5)}")
        print(f"  rogue-ack in VPA cycle: CDACK_flop={bits(v,4)} CDACK_core={bits(v,3)} DTACK_latch={bits(v,2)} SPIO_low={bits(v,1)} earlyack={bits(v,0)}")
    elif name == 'LIO':
        print(f"  kv_wr_cnt={bits(v,63,56)} kv_rd_cnt={bits(v,55,48)} bd_nz=0x{bits(v,47,40):02X} vma_cnt={bits(v,39,32)}")
        print(f"  bd_at_wr=0x{bits(v,31,24):02X} kv_last_wr(IO_D@wr)=0x{bits(v,23,16):02X} pp_io_nz=0x{bits(v,15,8):02X} kv_last_addr={bits(v,7,4)}")
    elif name == 'LCOP':
        print(f"  so_cnt={bits(v,63,56)} ack_cnt={bits(v,55,48)} l_in=0x{bits(v,47,40):02X} l_out=0x{bits(v,39,32):02X}")
        print(f"  kbdout_cnt={bits(v,31,24)} kbdin_cnt={bits(v,23,16)} DQ={bits(v,15)} ACK={bits(v,14)} _READY={bits(v,13)} ON={bits(v,12)} muxsel={bits(v,11,10)} kbdrst={bits(v,9)} kbd_in={bits(v,8)}")
        print(f"  KBD_out={bits(v,7)} via_pb0={bits(v,6)} via_ddrb0={bits(v,5)}")
    elif name == 'LKBD':
        print(f"  kbdsig_edges={bits(v,31,24)} kbdwire_edges={bits(v,23,16)} kbd_out_sig={bits(v,15)} kbd_wire={bits(v,14)}")
    elif name == 'LBUS':
        print(f"  mismatch={bits(v,63)} valid={bits(v,62)} first_addr_lo=0x{bits(v,61,50):03X}(word[12:1])")
        print(f"  first_wr=0x{bits(v,47,32):04X} first_rd=0x{bits(v,31,16):04X}")
        print(f"  match_cnt={bits(v,15,8)} mismatch_cnt={bits(v,7,0)}")
