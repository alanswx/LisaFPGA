# Lisa MiSTer Core — Quartus / Hardware Bring-up Handoff

Target: Cyclone V, DE10-Nano `5CSEBA6U23I7`, Quartus 17.0.2.
Board reachable at **192.168.1.196** (MiSTer Remote API on `:8182`, SSH root/`1`,
USB-Blaster JTAG as "DE-SoC").

## Current status (as of this handoff)

- **Builds clean**: `quartus_sh --flow compile Lisa` → 0 errors, produces
  `output_files/Lisa.sof` + `Lisa.rbf`. Timing essentially closed
  (~−0.6 ns worst setup; see §5).
- **Runs on hardware and boots**: the core powers on, comes out of reset, runs at
  **60 fps**, and the framework detects the correct **720×364** resolution (shown
  in the MiSTer OSD).
- **KNOWN REMAINING ISSUE — video content**: the screen is **black with a single
  white vertical line**. Geometry/timing is correct; the *picture content* is not
  the expected Lisa boot screen. This is the next thing to debug (see §6).
- **⚠️ The working tree has UNCOMMITTED changes** beyond commit `bfd2755`
  (the single-clock conversion). All the hardware bring-up fixes + debug
  instrumentation below are uncommitted. Decide what to keep before committing.
- **⚠️ Debug instrumentation is still compiled in** and must be removed for a clean
  release build (see §4).

---

## 1. Architecture: single clock + clock enables (committed as `bfd2755`)

The core was converted from six gated/muxed/divided clocks on **3 PLLs** (which
overflowed the Cyclone V PLL budget and would not route) to **one 81.50016 MHz
master `clk_sys`** off a single PLL, with every former clock replaced by a
one-cycle **clock-enable strobe** (`@(posedge clk_sys) if (x_en)`).
- `rtl/pll.sv`: single PLL → `clk_sys` + phase-shifted SDRAM `clk_mem`.
- `rtl/clock_divider.v`: enable generator (`dotck_en`, `c16m_en`, `c5m_en`,
  `copck2x_en`, `sccck2x_en`, `usbclk_en`); DOTCK speed select folded in.
- Converted: top, CPU_board (fx68k `enPhi1/2` gated by `dotck_en`), IO_board,
  mem_board_2mb/512k, usb_*_interface, Lite_Adapter.
- `z8530_scc` (SCC) and `AM9512_FPU` run on raw `clk_sys` (serial/FPU rate
  deferred — non-critical for bring-up). See `todo.md`.

## 2. Hardware bring-up fixes (UNCOMMITTED)

Found via JTAG instrumentation on the real board. All were pre-existing latent
bugs exposed on the first hardware boot (the core had only ever been simulated).

1. **Never powered on.** `Lisa.sv` tied `_PWRSW` to `1'b1`; the COP only powers
   the machine on when it sees a power-button *falling edge*. Added an auto
   power-on: hold `_PWRSW` high ~0.4 s after config, then pulse it low. → `ON=1`.
2. **Video 4× too fast (80 MHz DOTCK).** The DOTCK speed-select mapping was
   inverted vs. the OSD "CPU Speed 1x/2x/3x/4x" menu (default `00`→80 MHz instead
   of 20 MHz). Fixed the `speed_sel` decode in `rtl/clock_divider.v`
   (`00`→20 MHz … `11`→80 MHz). → 60 fps.
3. **`CE_PIXEL` tied to 1.** With `CLK_VIDEO=clk_sys` (81.5 MHz), the scaler
   sampled pixels 4× too fast → framework saw 1×1. Exposed the DOTCK pixel-enable
   (`dotck_en`) from top as `pixel_ce` and drove `CE_PIXEL` from it (`Lisa.sv`).
4. **Broken DE (data-enable) reconstruction.** The old `Lisa.sv` hack rebuilt DE
   from sync with hardcoded windows (120/840/20/384) that never matched the real
   timing (VGA_DE never asserted). Replaced with a clean **720-wide** window:
   vertical active = `~VA_overflow`; horizontal active armed by `_HSYNC` rising
   and started on the first `_clr_vid_clk` fall each line, held 720 px. Verified
   on hardware: `de_hmax=720`, `de_total≈261590` (720×363). → OSD shows 720×364.
5. **`sys/sys_top.sdc`** clock-group filter fixed (`*|pll|pll_inst|` →
   `*|main_pll|`) so core/framework clocks are declared async (killed false
   timing violations; −39.7 ns → −0.6 ns).

## 3. How to build / deploy / test (the working loop)

```bash
# Build (~24 min)
/home/alans/intelFPGA_lite/quartus/bin/quartus_sh --flow compile Lisa
# Fast syntax check only (~1 min): quartus_map --read_settings_files=on --write_settings_files=off Lisa -c Lisa

# Deploy the .rbf to the MiSTer and launch it via the Remote API
sshpass -p 1 scp -o StrictHostKeyChecking=no output_files/Lisa.rbf root@192.168.1.196:/media/fat/Lisa.rbf
curl -s -X POST http://192.168.1.196:8182/api/launch -H "Content-Type: application/json" -d '{"path":"/media/fat/Lisa.rbf"}'

# Screenshot: POST to take one, then GET it (the API downscales; OSD resolution is authoritative)
curl -s -X POST http://192.168.1.196:8182/api/screenshots
curl -s http://192.168.1.196:8182/api/screenshots        # list; newest path is under LISA/
curl -s "http://192.168.1.196:8182/api/screenshots/LISA/<file>.png" -o shot.png
```

You can also JTAG-program the `.sof` directly with
`quartus_pgm -c DE-SoC -m jtag -o "p;output_files/Lisa.sof@2"` (device @2 is the
5CSEBA6), but launching the `.rbf` via the API is cleaner (proper HPS/hps_io init).

## 4. Debug instrumentation currently compiled in (REMOVE for release)

Added for bring-up; read/controlled over JTAG with `quartus_stp`. Scripts are in
the session scratchpad (`read_probe.tcl`, `poke_probe.tcl`, `read_video.tcl`).

- `rtl/debug_issp.sv` — ISSP instance **"LDBG"**: probe reports pll_locked / ON /
  reset / frame-rate; source can override speed-select and force the core on.
  Referenced from `top.sv` (`u_debug_issp`, ports `pll_locked`, `dbg_speed_*`,
  `dbg_force_on`); listed in `files.qip`.
- `Lisa.sv` ISSP instance **"LVID"** (`u_vid_probe`) — measures DE active area
  (`de_hmax`, `de_total`, `de_ever`).
- Extra ports threaded top↔Lisa for debug: `pll_locked`, `pixel_ce`, `dbg_va`,
  `dbg_clr`.

To remove: delete `debug_issp.sv` (+ its `files.qip` line and `u_debug_issp`
instance), the `u_vid_probe` block in `Lisa.sv`, and the debug-only ports/overrides
in `top.sv`. KEEP: `pixel_ce`/`CE_PIXEL`, the DE reconstruction, the auto power-on,
the speed-map fix, the SDC fix — those are real fixes.

Reading a probe (needs `package require ::quartus::insystem_source_probe`, and
`get_insystem_source_probe_instance_info` must be called BEFORE
`start_insystem_source_probe`):
```
quartus_stp -t read_video.tcl   # prints LDBG status + LVID de_hmax/de_total/de_ever
```

## 5. Timing

Essentially closed after the SDC fix: worst setup ≈ −0.6 ns, hold ≈ −0.6 ns, on
the `clk_sys` domain (a few paths, likely the SCC/FPU on raw clk_sys). Close with
`set_multicycle_path`/`set_false_path` on those blocks or the deferred SCC/FPU
enable conversion. Details in `todo.md`.

## 6. Next steps (priority order)

1. **Video content bug (black screen + white vertical line).** Geometry is right
   (720×364) but the picture is wrong. Investigate: is the CPU actually executing
   the boot ROM (add a CPU-liveness probe: watch fx68k address/bus activity)? Is
   the framebuffer being written/read (video RAM path)? Is `VID` polarity /
   pixel-shift correct? The single white line on black is the key clue — it may be
   one column of the raster with everything else blanked, pointing at the video
   fetch/shift path (CPU_board `vid_shift_reg`, `vid_addr_counter`) under the new
   clk_sys+dotck_en clocking. Compare against a simulation of the same scene.
2. Remove the debug instrumentation (§4) once the picture is correct.
3. Close timing (§5) and do the deferred SCC/FPU enable conversion (`todo.md`).
4. Functional check of keyboard/mouse via the COP, and SCC serial.
5. Decide what to commit (working tree is currently dirty beyond `bfd2755`).

---

## 7. Session 2026-07-06: SDRAM path root-caused and replaced

(Corrected symptom: the screen is **white with one vertical dashed black bar**,
not black-with-white-line. NOTE `top.sv` inverts INVID, so CPU_board runs with
INVID=1 → **VID is inverted**: white = framebuffer bits 0 (uninit SDRAM), and
the black bar is the shift register draining INVID=1 past the last word of each
line. The video path is fine; the boot image is simply never painted.)

Findings/changes this session (all uncommitted, on top of the earlier dirty tree):

1. **Replaced the SDRAM controller.** The old `rtl/sdram.v` was the 2015 MiST
   Minimig controller run out of spec (designed 64 MHz + phase-locked 8 MHz;
   driven at 81.5 MHz + 10.19 MHz with a guessed PLL phase). Now
   `rtl/sdram_ctrl.sv` = Sorgelig's proven MiSTer controller (Genesis_MiSTer),
   3 req/ack ports, generates SDRAM_CLK itself via DDIO (180° from clk_sys) —
   `clk_mem`/PLL phase no longer used. Refresh retuned (600 cyc = 7.36 µs).
   `Lisa.sv` has a new cycle→req/ack bridge: reads issue when RAS+CAS assert;
   writes wait for the 68k byte strobes (UDS/LDS can lag CAS on writes).
2. **SDRAM self-test passes on hardware** (ISSP probe **"LRAM"**, controller
   port 1): writes/reads/retention verified live. LRAM layout (64-bit):
   `{st_rd1[16], st_rd0[16], st_loops[4], st_state[4], lisa_wr_cnt[12], lisa_rd_cnt[12]}`;
   expect st_rd1=0x5A3C st_rd0=0xA5C3.
3. **RAM_SEL trap found:** OSD default status=0 → `RAM Size = 512KB` →
   `mem_board_2mb` silently drops (CAS-inhibits) every access with A19|A20 set —
   including **all video fetches** (video page sits at top of RAM) and the ROM's
   sized stack. Result: CPU ran ~512 writes then wedged; rd/wr counters frozen.
   Board config is written directly (no OSD nav needed):
   `printf '\x18\x00...' > /media/fat/config/LISA.CFG` (16-byte little-endian
   status word; 0x18 = status[4:3]=11 = 2MB), then relaunch the core.
   With 2MB set, video reads + CPU writes run continuously.
4. **CPU hang root-caused via PC probe + boot ROM listing.** ISSP probe
   **"LCPU"** (CPU_board.sv) captures the last program-space fetch address; it
   showed the CPU parked at ROM 0xFE04B2-B6. The bitsavers boot ROM listing
   (`Lisa_Boot_ROM_Asm_Listing.TEXT`, matches our 3A ROM byte-for-byte here)
   identifies that loop as the **"no memory found" dead-end** after memory
   sizing (`MEMSIZ`/`CHKMEM` at 0x446/0x508) finds no RAM at any 128K boundary.
   CHKMEM is a plain write/read-back compare (no parity tricks). Sizing math
   confirmed the old freeze: 4 in-range boundaries x 32 retries x 4 writes =
   the exactly-512 writes seen with RAM_SEL=512KB.
5. **THE ROOT CAUSE (fixed in this working tree): `D_SRAM` was never
   connected in Lisa.sv.** `top` exchanges all RAM data over its tri-state
   `D_SRAM` inout (drives write data when SRAM_BUS_DIR=0, samples read data
   otherwise — top.sv:1141-1146). Lisa.sv passed a bare undriven `D_SRAM` wire
   to top and hooked the SDRAM controller's din/dout to two dangling local
   wires (`DOUT_SRAM`/`DIN_SRAM`). So RAM writes stored a constant and RAM
   reads returned an undriven bus — CHKMEM saw "all bits in error" everywhere
   -> "no memory" -> park loop. This predates the controller swap (the old
   MiST sdram.v hookup had the same dead wiring), so it was **the** video-bug
   root cause all along. Fix: `assign D_SRAM = _OE_SRAM ? 16'bZ : DIN_SRAM;`
   (drive controller read data during reads) and bridge write data sampled
   from `D_SRAM` (top drives it during writes).
6. Diagnostic probes now in: **"LBUS"** (Lisa.sv) — port-level write->read-back
   checker (match/mismatch counters + first mismatch capture); **"LCPU"**
   (CPU_board.sv) — PC + bus-error count + the whole HDER/SFER/NMI/IPL chain
   with sticky flags. NOTE: before the D_SRAM fix, LBUS "passed" vacuously
   (it compared the same dead wires on both sides).
7. Probe scripts in scratchpad: `read_probes.tcl` (dumps all ISSP instances),
   `decode_probes.py` (pretty-prints all fields; pipe stp output into it).
8. If reads turn out flaky after the D_SRAM fix (SDRAM round-trip is ~120-200ns
   after CAS vs ~60-70ns for the original 55ns SRAM), the follow-up fix is to
   gate CDACK_core (CPU_board.sv:1089) for CPU RAM *reads* on the bridge's
   read-data-returned signal, inserting 68k wait states. Not applied yet —
   one variable at a time.

### After the D_SRAM fix (2026-07-06 later): BIG progress

- **The machine boots into ROM diagnostics and DRAWS THE SCREEN**: gray dithered
  desktop + alert + a crossed-out icon with error code **50**. Memory sizing
  passes, RAM read-back verified (LBUS all-match), CPU runs the full test chain.
- **Error 50 = `EVIA1` = COPS VIA (keyboard VIA1) error** — the ROM's VIA1
  timer-latch read/write test (`VIATST`/`VIARW`, listing 0x7B8/0x7D0, looped
  from `VIA1CHK` 0x8B0) fails. Structural lead: the parallel VIA (VIA2, same
  via6522 model) PASSES its identical test, but VIA2 is an async/DTACK device
  while **VIA1 is accessed via the 68k's 6800-style VPA/_VMA/E-clock cycle**
  (IO_board.sv:1142 asserts VPA for kbd-VIA/disk/SCC; CS_KBD_VIA =
  ~_CS_KBD_VIA & ~_VMA). So suspect the fx68k VPA->VMA/E path under the
  single-clock conversion. New ISSP probe **"LIO"** (IO_board.sv) counts
  VPA requests / VMA responses / kbd-VIA reads+writes and captures last
  read/write data + reg address — read it to see which stage is dead.
- **Video formatting fix**: the picture content was correct but horizontally
  rotated (DE window opened on the WRONG _clr_vid_clk edge — the old hack used
  its first FALL after HSYNC, which is the END of active). New DE = _clr_vid_clk
  (high exactly for the 45-word active region) delayed through a dot-rate pipe;
  delay defaults to 18 dots and is **trimmable live over JTAG** via the LVID
  ISSP *source* (5 bits, 0 = use default 18) to center the picture without
  rebuilds.
- MiSTer screenshots decimate 720->230 horizontally (the dithered desktop
  aliases into stripes); judge the picture on the real monitor or fix up the
  scaler config.
- **VIA1 failure narrowed to the write-data path**: LIO probe shows the whole
  VPA->VMA->CS->E chain running (all counters spin), reads return the actual
  T1 latch contents — but the latch holds 0x00 and every captured write shows
  data 0x00. The 0x00 clear-writes land; pattern writes (0xFF..) arrive as
  0x00. I.e. **the CPU write data reaches the kbd VIA as constant zero** via
  BD -> IO_board `IO_D = (A[12] & ~_INTIO & ~READ) ? BD_in[7:0]` (IO_board.sv
  ~1430). LIO now also captures BD_in[7:0] at the exact via6522 write-strobe
  moment (wen & E_neg_phase): zero BD = CPU/top-mux side dead for 6800-style
  cycles; nonzero BD with zero IO_D = the IO_D driver condition.
- **Write-data hunt status (latest)**: at the E-fall strobe of VPA writes,
  CPU_board sees `_DBON=0 (driver on), _SPIO=1, _AS=0` — but `UD_CPU_out[7:0]
  == 0`, i.e. **fx68k's own oEdb carries zero**; and the sticky `bd_nz`
  (any nonzero BD during a kbd-VIA CS write window, over minutes of looping
  0xFF test writes) stays ZERO. Verified: rtl/fx68k.sv + microrom/nanorom are
  byte-identical to upstream alexthecat123/LisaFPGA (which boots on Xilinx),
  and the MiSTer port's IO_board.sv was identical to upstream pre-conversion;
  enPhi/E-strobe conversion is semantically equivalent.
- **fx68k EXONERATED by simulation**: scratchpad/fxsim has a Verilator
  testbench (needs the locally built Verilator 5.024 at
  scratchpad/verilator-src/bin, flags `--binary --timing -Wno-fatal
  -Wno-BLKANDNBLK`; system verilator 4.2 MIS-SIMULATES the Nanod struct!).
  It runs fx68k with Lisa-style enPhi (1-in-4) + combinational VPA decode and
  the exact failing sequence (MOVE.B #0,(A0) then MOVE.B D3,(A0), A0=FCDD8D):
  BOTH writes assert VMA and deliver correct data (00 then FF) at the E fall.
  So the bug is in the Lisa-side wrappers under Quartus. NOTE: the TB also
  found fx68k halts if reset releases so enPhi1 comes first (BeI zero-init
  glitch) — TB releases reset aligned to enPhi2; not our hardware issue.
- Rogue-ack detectors (probe LCPU[4:2]): CDACK_flop / CDACK_core /
  DTACK_latch NEVER assert during VPA cycles — those paths are clean. The
  remaining early-ack path is `_CDACK = ~(CDACK_flop | ~_SPIO)`: **a glitch
  low on _SPIO instantly acks the cycle** (combinationally into fx68k DTACKn)
  and would end the 6800 cycle before its E data phase — matching "0xFF
  writes complete but never coincide with a CS window". Detectors for this
  (LCPU[1:0]: SPIO_low-in-VPA-cycle, earlyack=_CDACK-while-VMA-idle) are in
  the current build. If confirmed, chase the _SPIO decode (MMU/MALE latch
  timing, possibly Quartus-vs-Vivado tri-state resolution of the muxed A bus)
  and consider gating the _SPIO ack term with registered/latched decode.
- Housekeeping: two "Top-level design entity Lisa is undefined" build
  failures were caused by running quartus_sh from the scratchpad cwd (chained
  `cd` in the same Bash call). Always launch builds from the repo root.
- **Trace buffer (ISSP "LTRC", CPU_board.sv)**: 128-sample dot-rate ring, JTAG
  re-arm (source bit7 rising), trigger = VPA read-cycle start (source bit6=1
  switches to write trigger), readout source[6:0]=addr. Scripts:
  scratchpad/read_trace.tcl + decode_trace.py. First capture of a VPA WRITE
  showed a TEXTBOOK cycle (AS->VPA(+6dots)->VMA->E->data 0x00 held thru E-fall)
  — and 0x00 is CORRECT: those writes are CLR.B! So writes were never broken;
  the ROM's first compare (read-back of the cleared latch vs 0) fails =>
  **the kbd-VIA READ path to the CPU is the broken direction**. Current build
  traces UD_CPU_in (what the 68k reads) around VPA reads.
- **Config audit vs upstream (README appendix + XDC)**: RAM/SPEED/GPIO(spoof_88
  off)/KBD_SEL/MOUSE_SEL(0=our USB adapters via native lines)/HDD_SRC all
  correct. CPU_ROM_SEL=0 selects **H** (recommended), IO_ROM_SEL=0 selects
  **A8** (recommended) — OSD labels were backwards, now fixed ("H ROM,3A ROM"
  / "A8 ROM,40 ROM"). **INVID was wrong**: upstream REG jumper = pin high,
  we tied 0 -> inverted video. Fixed to `.INVID(1'b1)` in Lisa.sv (boot
  screen is now black-at-poweron; alert boxes white like a real Lisa).

### ERROR-50 ROOT CAUSE FOUND AND FIXED (Quartus 'z-as-1 tri-state resolution)

The LTRC trace at the top-level BD mux showed **BD_OE_CPU and BD_OE_IO stuck
at constant 1**. Cause: `BD_OE_int` in both CPU_board and IO_board was a
`tri0` net with multiple `cond ? 1'b1 : 1'bz` drivers. **Quartus resolves
internal 'z as '1'** (Vivado, the original target, builds priority muxes
instead), so these OE nets synthesized to constant 1: top's BD mux always
selected the CPU board's internal bus (which idles at 0xFF under the same
resolution) and the 68k could NEVER read the I/O board — VIA reads returned
0xFF, the boot ROM's VIA test first compare failed, error 50. VIA *writes*
were always fine (verified by trace). Note the active-LOW z-driver nets
(_VPA/_IAK/_SFER wired-OR) work CORRECTLY under 'z-as-1 (open-collector
semantics), which is why only the read path broke. FIX: replaced all six
BD_OE z-drivers with explicit ORs (CPU_board.sv near MREAD; IO_board.sv
~line 386). Grep pattern to audit any future ports:
`grep -rn "1'b1 : 1'bz" rtl/` (active-high z-drivers are the hazard).

All JTAG debug scripts now live in **debug/** in the repo (read_probes.tcl,
read_trace.tcl + decode_probes.py / decode_trace.py, set_de_delay.tcl) so
they survive session scratchpads.
- **DE reconstruction v2**: raw `_clr_vid_clk`-as-DE made the framework
  mis-detect width (36px screenshots) even though the monitor looked right.
  Reverted to the HSYNC-referenced window (framework-friendly) but opening on
  the first _clr_vid_clk RISING edge (start of active; the old code used the
  falling edge = end of active, which rotated the picture) + JTAG-trimmable
  pipeline delay (LVID source, 0=default 18 dots;
  scratchpad/set_de_delay.tcl <n> sets it live).

### Post-fix status (build 14+): error 50 GONE, boot reaches COPS init

- Confirmed on hardware: BD mux arbitration now correct (LTRC trace shows
  OEIO/OECPU proper, VIA IFR data delivered to the 68k through the whole VMA
  window). The ROM passes memory sizing, RAM test, VIA tests, parity/bus-error
  diagnostics (HDER path exercised with masking), plays boot tones, and parks
  in **ReadCOPS** (listing 0x2DBE): polling VIA1 IFR bit 1 (CA1) for the COP
  keyboard controller's startup byte, which never arrives.
- Next chapter: COP (t420) -> VIA1 handshake. New ISSP probe **"LCOP"**
  (IO_board.sv): SO/ACK edge counters, last L-bus bytes both directions,
  KBD-line activity counters, live {DQ, ACK, _READY, ON, muxsel, kbdrst}.
  The COP definitely RUNS (it handled soft power-on: ON=1), so suspects are
  its SO/L-bus output path, the CA1 edge/PCR config, or the COP firmware
  stuck polling the keyboard line (our usb_keyboard_interface emulates the
  keyboard on the KBD serial line).
- INVID fix + OSD label fixes deployed with the same build.

### Keyboard line root cause (same tri-state disease, opposite polarity)

LKBD/LCOP probes proved NOBODY was driving the keyboard serial line low
(adapter kbd_out_sig=1, IO-side KBD_out=1, VIA PB0 released) yet the shared
net read 0: the internal z-net (Lisa.sv `kbd_serial_wire` + top's `inout KBD`)
resolves to constant 0 under Quartus (no pull-up on internal nets). The COP
saw a permanently jammed keyboard. Meanwhile LCOP shows the COP now queues
its proper 0xFF reset code to the 68k. FIX (build 17): top's `inout KBD`
split into `KBD_line_in`/`KBD_line_out`; Lisa.sv computes the open-collector
wired-AND explicitly (`kbd_serial_wire = kbd_line_out_top & kbd_out_sig`).
NOTE for future ports: ANY internal inout/z-net from the Xilinx design is
suspect on Quartus; convert to explicit in/out + AND (open-collector) or
mux (point-to-point). Audit candidates: ESProFile PD bus, D_SRAM (already
handled), the internal A/BD/MD/IO_D buses (work by accident of one-hot
driver + resolution; watch for weirdness).

### Build 18: ProFile mount + mouse fixes (user-reported)

- **ProFile mount was wired to the wrong MiSTer mechanism**: CONF_STR had
  `F,IMG` (ioctl file download) but profile.sv uses the SD block API
  (sd_lba/sd_rd/sd_wr/sd_buff). Changed to **`S0,IMGVHD,Mount Hard Disk`**
  so the selected image is mounted as a block device. (Future nicety:
  profile.sv has no img_mounted input — it answers the Lisa even with no
  image; gate readiness on img_mounted eventually.)
- **Mouse bytes were mis-mapped**: hps_io ps2_mouse = {[24] toggle,
  [23:16] Y delta (PS/2 Y+ = up), [15:8] X delta, [7:0] flags/buttons
  (bit0=L)}. Old wiring fed the FLAGS byte (negated) as Y and Y-delta bits
  as buttons. Fixed: dx=[15:8], dy=-[23:16], buttons={[2],[1],[0]}.
- Video alignment: still slightly off per user; use debug/set_de_delay.tcl
  N (1..31, 0=default18) live while watching the monitor to center it, then
  bake the winning value into Lisa.sv as the new default.

### Video timing FULLY SOLVED (session 2026-07-06, builds 20-32)

The core boots to the "STARTUP FROM" screen and the video is now a clean,
stable, correctly-detected 720x364 that fills the screen. The long road there,
and the final architecture, in Lisa.sv:

**Root facts (measured on hardware via the LVI2 raw-edge probe, per frame):**
- `_HSYNC` = ~380 edges = exactly ONE clean pulse per line (active AND blank).
  THIS is the only reliable per-line reference.
- `VA_overflow` = 1 rise + 1 fall per frame = clean frame boundary (high during
  vertical blank).
- `_clr_vid_clk` = ~575 edges/frame = NOT one-per-line (toggles ~1.5x/line).
  Do NOT use it for line timing — that was the original bug (it chopped DE into
  extra pulses; earlier reconstructions gave 11/443/517/529-line garbage).
- The Lisa's active video STRADDLES the raw `_HSYNC` pulse (content lives at
  ~[370..lineend)+[0..194) in the _HSYNC-referenced line), so `_HSYNC` cannot be
  used as VGA_HS directly and the DE window truncated past h_de_start~176.

**Final raster generation (all in the pixel_ce / dotck_en domain):**
- Line marker = raw `_HSYNC` rising. A per-line dot counter `hcnt` is reset
  `origin_delay` (=144) dots after `_HSYNC` — i.e. in the blanking GAP between
  the two wrapped content halves — so the 720 active dots become contiguous.
- `VGA_HS` = REGENERATED clean pulse at that origin (hcnt < HS_WIDTH=64), in the
  blank. (Raw _HSYNC as VGA_HS put the sync inside active video.)
- `VGA_DE` horizontal = `hcnt in [h_de_start, h_de_start+720)`, h_de_start=20.
- `VGA_DE` vertical = a FIXED 364-line counter (`vcnt`) clocked by `_HSYNC`,
  anchored to `VA_overflow`'s falling edge (`!dbg_va && va_line`, where
  `va_line` latches dbg_va of the previous line). Using VA_overflow's raw extent
  gave 379-443 lines; the fixed counter guarantees exactly 364.
- `VGA_VS` = regenerated: on `VA_overflow` rising, arm; assert on the NEXT
  `_HSYNC` (a line boundary in the blank), hold 3 lines. (Raw _VSYNC and
  VA_overflow both rise MID-line, which made video_freak/ascal — which reset
  their line counter on the VS edge and measure the first line as the picture
  width — read a partial line, e.g. 1/36/77 px wide.)
- `origin_delay` (source[15:11] x16, def 144) and `h_de_start` (source[10:0],
  def 20) are LIVE-tunable via the LVID ISSP source. Scripts:
  debug/set_vid.tcl <origin_src> <h_de_start> (also prints first_w),
  debug/set_de_delay.tcl <h_de_start>.

**Why the OSD/screenshot kept disagreeing with my probes:** MiSTer's
`video_freak` measures picture width from the FIRST active line after VS only
(`if(!vcpt) hsize<=hcpt`). Any partial/glitch DE line right after VS (from
VA_overflow dropping mid-line, or the free-running hcnt wrapping in the blank
with a stuck v_active) got measured as the width. The fixes above eliminate all
blank-region and partial DE. Verify with LVID `first_w` (want 720) and
`n_lines` (want 364); the API screenshot only renders cleanly once detection is
right (garbage color-noise thumbnail otherwise).

**Also fixed this session:** SDRAM controller swap, D_SRAM bus connection, the
tri-state-resolution disease (BD_OE / keyboard line / ProFile PD bus — Quartus
resolves internal `cond?1:z` nets to constant 1, unlike Vivado; converted all to
explicit OR/mux), error-50 (COPS VIA read path), keyboard + mouse event pulsing
(MiSTer ps2_mouse[24] is a TOGGLE not a strobe), ProFile mount (F,IMG -> S0),
INVID polarity, OSD ROM labels. See git diff. Remaining: strip debug probes
(LVID/LVI2/LCPU/LIO/LCOP/LKBD/LMOU/LRAM/LDBG) for release; close timing; SCC/FPU
enable conversion; confirm ProFile actually boots an OS.

### ProFile boot debugging (session 2026-07-06 late) — diagnosis, not yet fixed

Instrumented profile.sv with ISSP probe **"LPRO"** (FSM state / max_state,
command, block#, _CMD/_PSTRB edge counts, SD rd_acks, img_mounted, _PRES) and
drove tests with an **MGL** (`/media/fat/lisa_profile.mgl`, `type="s" index="0"`
mounts the image into S0 — launch via the API, no user needed). Findings:

1. **The ProFile parallel protocol WORKS.** The Lisa's presence check succeeds
   (the ProFile icon appears on the STARTUP FROM screen; LPRO shows one _CMD
   edge). The emulator is healthy.
2. **The Lisa never auto-boots the ProFile — it sits in the STARTUP FROM menu**
   (CPU parked in ReadCOPS at 0xFE2DCA polling for a boot-device selection;
   LPRO max_state stays IDLE, rd_acks=0 — no disk read is ever issued). So the
   disk-read path is currently UNTESTED (the Lisa doesn't try).
3. **Why the menu appears (root cause):** boot ROM logic (Lisa_Boot_ROM listing)
   — `RSTSCAN` (0x9C2) resets the keyboard (RSTKBD 0xAAA pulses VIA1 PB0) then
   immediately polls to consume the keyboard **reset code RSTCODE=0x80** (then
   the ID byte, ≤0xDF, saved as KEYID). If it finds nothing it exits ("may be
   old keyboard"). THEN `KEYSCAN` (0x1214) reads key codes and sets **BTMENU**
   (→ menu) for ANY downstroke key except alpha-lock(0xFD)/mouse-button(0x86).
   The COP presents 0x80 to the Lisa AFTER RSTSCAN gives up, so KEYSCAN reads
   0x80 as a downstroke → BTMENU → menu → no auto-boot. It's a **timing race**
   between the COP's keyboard-reset report and RSTSCAN's poll window.
4. **The keyboard adapter is CORRECT** (verified against the ROM): 0x80=RSTCODE,
   0xBF=US-keyboard ID is exactly the protocol; ps2_key decode
   ([10]toggle/[9]pressed/[8]ext/[7:0]scancode) is right. **The 0x80 originates
   in the COP** (t420), not the adapter: muting the adapter output (kbd_mute,
   LKBD source bit0) leaves the COP still presenting l_out=0x80 and the menu
   still pops. So the fix is NOT in usb_keyboard_interface.
5. **Likely fix direction:** the COP (t420) runs on copck2x_en clock-enables
   (single-clock conversion). Its keyboard-reset-report timing races the ROM's
   RSTSCAN. Either tune the COP enable timing / keyboard-reset response so 0x80
   lands inside RSTSCAN's window, or (workaround) suppress the reset-code path
   during the initial boot scan at the COP/VIA1 level (NOT at the adapter — that
   was tried and does not work). To verify the disk-read path independently,
   select the ProFile from the menu (click the icon, or Apple+3 = boot ProFile,
   0xF2 is the '3' boot key) and watch LPRO max_state / rd_acks advance.
