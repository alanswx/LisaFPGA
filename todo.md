# TODO — clocking refactor follow-ups (verify/fix after it runs on the FPGA)

## What was done
The Lisa core was converted from many derived/gated/muxed clocks (which overflowed
the Cyclone V PLLs and could not route) to a **single clock + clock-enable**
architecture:

- **One PLL** (`rtl/pll.sv`) produces only `clk_sys` (81.50016 MHz master) and its
  phase-shifted SDRAM twin `clk_mem`. Both `dotck_mmcm` and `clock_divider`'s old
  PLLs are gone.
- `rtl/clock_divider.v` is now a pure **enable-strobe generator** in the clk_sys
  domain: `dotck_en` (speed-selectable), `c16m_en` (÷5), `c5m_en` (÷16), and
  `copck2x_en` / `sccck2x_en` / `usbclk_en` (phase-accumulator carry strobes).
- Every module now runs on `clk_sys` and gates its registers with the matching
  enable (`@(posedge clk_sys) if (x_en)`), instead of a dedicated clock. Converted:
  top.sv, CPU_board, IO_board, mem_board_2mb/512k, usb_keyboard_interface,
  usb_mouse_interface, Lite_Adapter. Enable-ready leaf models (fx68k `enPhi1/2`,
  6502 `.phi`, via6522 `.rising/.falling`, COP `.ck_en_i`, LS259 `_G`,
  LS323 `.clk_en`) were fed `clk_sys` with their enable gated by the rate strobe.
- ON-gating and the DOTCK speed mux collapsed into enable masking (single domain,
  no more per-domain clock synchronizers / behavioral clock muxes).

## MUST verify (functional — could not be checked here)
This was validated structurally (it must build + place + **route** now), but NOT
functionally. Verify in simulation and/or on hardware:
1. **Boot + video**: ROM boots, DOTCK-domain video timing correct at 20 MHz.
2. **Keyboard/mouse via the COP** (6504/COP421 path in IO_board).
3. **68000 timing**: fx68k is fed `clk_sys` with `enPhi1/enPhi2` gated by `dotck_en`
   (CPU_board). Confirm bus timing / E-clock phases still line up.
4. **Memory**: MMU_RAM_2148, RAM_matrix and the SDRAM controllers inside the
   mem_boards were left running on raw `clk_sys` (full speed) rather than a
   dotck_en enable. Writes are idempotent across the fast cycles, but confirm no
   double-action side effects.

## Timing closure (in progress)
- The design now **fits, routes, and builds** to `output_files/Lisa.sof` + `Lisa.rbf`.
- Fixed `sys/sys_top.sdc` line 14: the core-PLL clock-group filter was
  `*|pll|pll_inst|...` which did NOT match our PLL (`emu|main_pll|...`), so paths
  between clk_sys and the framework audio/HDMI/HPS clocks were analyzed as
  synchronous → huge FALSE violations. Changed it to `*|main_pll|...`. That took
  worst-case setup slack from −39.7 ns to −4.7 ns.
- After rebuilding with the corrected SDC (fitter no longer chasing false paths),
  timing is essentially closed: **worst setup −0.609 ns** (TNS −5.6 ns, a few
  endpoints) and **worst hold −0.576 ns** (a single endpoint), both on the core
  `clk_sys` domain. The HDMI/audio/HPS framework domains now PASS.
- To reach full closure: the remaining sub-ns paths are almost certainly the SCC
  (z8530) and AM9512 FPU running at full clk_sys, plus an async-latch path. Close
  via (a) proper enable conversion of SCC/FPU, (b) `set_multicycle_path` on those
  blocks, and/or (c) constraining the async TTL-latch nets (`_CAS`, `_MALE`,
  `_AS`, `_MMUIO`, `_IOCY` — auto-detected as unconstrained clocks) with
  `set_false_path` or restructuring. The single −0.576 ns HOLD path matters most
  (hold can't be fixed by slowing the clock) — identify and fix it first.

## Known-deferred / intentionally approximate
1. **SCC serial (z8530_scc) runs at clk_sys.** Its `clk`/`pclk`/`sclk` were all tied
   to `clk_sys`, so Serial A/B **baud rates are wrong** (≈20× too fast) and the
   internal clk↔sclk CDC is now same-clock. `sccck_en` is generated and routed to
   IO_board but unused. To restore serial: add serial clock-enable ports to
   z8530_scc, guard its ~22 `sclk_a`/`sclk_b` blocks with `sccck_en` (Serial B) and
   a `c4m_en` (Serial A / pclk), and its bus blocks with `dotck_en`.
2. **AM9512 FPU runs at clk_sys** instead of ~2 MHz C2M (no enable port). Faster
   math; result is polled so likely fine, but confirm. Give it a `c2m_en` gate if
   needed (it needs an added enable port).
3. **C4M / C2M generation in IO_board is now dead** (their consumers moved to
   clk_sys). Left in place; can be deleted.
4. **DOTCK 60 MHz turbo** (`speed_sel==01`) is an irregular 3-of-4 enable pattern;
   20/40/80 modes are exact. See `clock_divider.v`.
5. **usbclk ~12 MHz** and **COPCK/SCCCK** rates come from phase accumulators
   (average frequency correct to ppm). USB HID host is stubbed on MiSTer anyway.
6. **SDRAM phase shift** is `9816 ps` (= −2454 ps), snapped to the PLL's legal
   307 ps grid (intended −2500 ps; 46 ps off, negligible).
7. `rtl/dotck_mmcm.v` is now an unused module (no longer instantiated). Can be
   removed from `files.qip`.
