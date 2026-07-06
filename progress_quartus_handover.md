# Quartus Compilation Handover & Progress Report

This document tracks the work to get the Apple Lisa MiSTer core (Cyclone V,
DE10-Nano `5CSEBA6U23I7`) compiling and closing timing in Quartus 17.0.2.

## Current status: **BUILDS to a bitstream** ✅

A full `quartus_sh --flow compile Lisa` succeeds:

- Analysis & Synthesis, Fitter, Assembler, Timing Analyzer: **0 errors**.
- Outputs: `output_files/Lisa.sof` and `output_files/Lisa.rbf` (the MiSTer bitstream).
- Timing is essentially closed: worst-case **setup −0.609 ns**, **hold −0.576 ns**
  on the core clock, both sub-nanosecond (down from −39.7 ns before the fixes).

Open items are tracked in **[todo.md](todo.md)** (functional verification, the last
sub-ns timing paths, and the intentionally-deferred SCC/FPU work).

---

## 1. Early fixes (synthesis / fitter)

1. **Hierarchical reference** (`Lisa.sv`/`top.sv`): exposed `usbclk` as a real port
   instead of a non-synthesizable hierarchical `assign`.
2. **Multiple constant drivers** (`profile.sv`): merged HPS cache write logic into
   the main state-machine clock block.
3. **Async resets** (`6502.v`/`via6522.v`): isolated the reset in the first
   conditional as Quartus requires.
4. **Missing entities** (`stubs.sv`/`files.qip`): re-added `usb_mouse_interface.sv`;
   created `rtl/stubs.sv` stubbing `BUFG`, `HDMI_Interface`, `usb_hid_host`. Later
   fixed the stub's `usb_dm`/`usb_dp` from `inout` to `output` (they were wired to
   `logic` variables in `top.sv`, which is illegal for `inout`).
5. **Wrapper port mismatch** (`Lisa.sv`): exposed `ADC_BUS`, `DDRAM_*`, `HDMI_*`
   ports on `emu` to match `sys_top.v`.
6. **RAM init synthesis** (`MMU_RAM_2148.sv`/`CPU_board.sv`): made the MMU SRAM write
   synchronous so Quartus can initialize the array on Cyclone V.

## 2. The real blocker: PLL budget → single-clock rewrite

Once synthesis passed, the **Fitter failed**: the core wanted three PLLs
(`main_pll`, `clock_divider`'s PLL, `dotck_mmcm`'s PLL) but only one fractional-PLL
slot reachable from the 50 MHz input was free after the MiSTer framework's PLLs, so
`dotck_mmcm` could not be placed. Reducing to fewer PLLs but keeping derived
*clocks* (counters/accumulators used as clock nets) then failed to **route** — the
fabric-routed derived clocks blew up hold timing.

**Fix (per project direction): one clock + clock enables.** The core was converted
to run entirely on a single 81.50016 MHz master `clk_sys` (straight off the one PLL,
on a global clock network), with every former clock replaced by a one-cycle
**clock-enable strobe**:

- `rtl/pll.sv` — now the **only** core PLL: `clk_sys` + phase-shifted SDRAM `clk_mem`.
- `rtl/clock_divider.v` — repurposed as the **enable generator**: `dotck_en`
  (speed-selectable), `c16m_en` (÷5), `c5m_en` (÷16), and `copck2x_en` /
  `sccck2x_en` / `usbclk_en` (phase-accumulator carry strobes). The DOTCK speed mux
  is folded in here.
- `rtl/dotck_mmcm.v` — no longer instantiated (its PLL is gone).
- Every module now uses `@(posedge clk_sys) if (x_en)` instead of a dedicated clock:
  `top.sv`, `CPU_board.sv` (incl. fx68k `enPhi1/2` gated by `dotck_en`),
  `IO_board.sv`, `mem_board_2mb/512k.sv`, `usb_keyboard_interface.sv`,
  `usb_mouse_interface.sv`, `Lite_Adapter.sv`. Enable-ready sub-models (fx68k, 6502
  COP, `via6522`, `LS259`, `LS323`) were fed `clk_sys` with their enable gated by the
  rate strobe. ON-gating and the DOTCK clock mux collapsed into enable masking.
- `z8530_scc` (SCC) and `AM9512_FPU` were left on raw `clk_sys` for now (serial/FPU
  rate deferred — see todo.md); non-critical for boot/video/keyboard bring-up.

## 3. Timing

`sys/sys_top.sdc` line 14 declares the core-PLL clocks asynchronous to the framework
audio/HDMI/HPS clocks, but its filter `*|pll|pll_inst|...` did not match our PLL
(`emu|main_pll|...`), so cross-domain paths were analyzed as synchronous → large
**false** violations. Changed the filter to `*|main_pll|...`; worst-case setup slack
went from −39.7 ns to −4.7 ns, and after rebuilding (fitter focusing on real paths)
to **−0.6 ns**.

---

## 4. Next steps

1. **Program `output_files/Lisa.rbf` on the DE10-Nano and verify functionally**
   (boot ROM, video, keyboard/mouse via the COP). The conversion was validated
   structurally — it builds, routes, and times — but not yet functionally.
2. Close the last sub-ns timing paths and do the deferred SCC/FPU enable work — all
   detailed in **[todo.md](todo.md)**.
