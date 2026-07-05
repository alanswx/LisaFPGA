# Quartus Compilation Handover & Progress Report

This document outlines the changes made to resolve compilation and synthesis errors in the Apple Lisa MiSTer core target, and provides instructions for the agent on the Quartus machine.

## 1. Summary of Resolved Issues

We have successfully resolved all syntax, structural reset, entity instantiation, and RAM initialization synthesis errors encountered in the previous builds.

### Key Fixes:
1. **妈妈board (Lisa.sv / top.sv) - Hierarchical Reference Error**:
   * Resolved a non-synthesizable hierarchical assignment (`assign usbclk_12M = core.usbclk;`) by exposing `usbclk` as a module output port on `top.sv` and mapping it directly in `Lisa.sv`.
2. **Profile Emulator (profile.sv) - Multiple Constant Drivers**:
   * Merged the HPS cache write logic into the main state machine clock block to prevent synthesis conflicts on `cache_data`.
3. **Asynchronous Resets (6502.v / via6522.v)**:
   * Restructured asynchronous reset always blocks to ensure the reset signal is isolated in the first conditional statement as strictly required by Quartus.
4. **Missing/Undefined Entities (stubs.sv & files.qip)**:
   * Added the missing `usb_mouse_interface.sv` back to `files.qip`.
   * Created a clean stubs file [rtl/stubs.sv](file:///Users/alans/Documents/development/LisaFPGA/rtl/stubs.sv) to define dummy entities for Xilinx-specific `BUFG` primitives and standalone-only modules (`HDMI_Interface`, `usb_hid_host`). Added it to `files.qip`.
5. **Wrapper Port Mismatch (Lisa.sv)**:
   * Exposed missing `ADC_BUS`, `DDRAM_*`, and `HDMI_*` width/height ports on the `emu` module in `Lisa.sv` to match the connections in the MiSTer framework's top-level wrapper `sys_top.v`.
6. **RAM Initialization Synthesis Error (MMU_RAM_2148.sv / CPU_board.sv)**:
   * Changed the MMU RAM write process from an asynchronous latch-based loop (which Quartus could not initialize on Cyclone V) to a synchronous register-based loop clocked on `DOTCK`.

---

## 2. Walkthrough of Modified Files

* [Lisa.sv](file:///Users/alans/Documents/development/LisaFPGA/Lisa.sv): Connected the exposed ports to match `sys_top.v`'s instantiation of `emu`.
* [rtl/top.sv](file:///Users/alans/Documents/development/LisaFPGA/rtl/top.sv): Declared the `usbclk` output port.
* [rtl/profile.sv](file:///Users/alans/Documents/development/LisaFPGA/rtl/profile.sv): Unified cache write drivers.
* [rtl/6502.v](file:///Users/alans/Documents/development/LisaFPGA/rtl/6502.v) & [rtl/via6522.v](file:///Users/alans/Documents/development/LisaFPGA/rtl/via6522.v): Corrected async reset templates.
* [rtl/stubs.sv](file:///Users/alans/Documents/development/LisaFPGA/rtl/stubs.sv): Stubbed `BUFG`, `HDMI_Interface`, and `usb_hid_host`.
* [files.qip](file:///Users/alans/Documents/development/LisaFPGA/files.qip): Added `stubs.sv` and `usb_mouse_interface.sv`.
* [rtl/MMU_RAM_2148.sv](file:///Users/alans/Documents/development/LisaFPGA/rtl/MMU_RAM_2148.sv) & [rtl/CPU_board.sv](file:///Users/alans/Documents/development/LisaFPGA/rtl/CPU_board.sv): Added `clk` input to the MMU SRAM model and clocked the write path to enable synthesis of the initialized RAM arrays.

---

## 3. Next Steps on the Quartus Machine

1. **Pull the latest commit**:
   ```bash
   git pull
   ```
2. **Run the compile / build command**:
   Execute the project's build command or compile script.
3. **Verify the error log (`err`)**:
   * If compilation succeeds, verify that the output `.rbf` or `.sof` file is generated.
   * If compilation fails, check the `err` file for any new synthesis, fitting, or timing errors.
