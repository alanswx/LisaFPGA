# TODO

## Current Status

The MiSTer port builds and the Verilator simulator runs headless. The active
debug target is ProFile boot from a mounted image.

The original Xilinx LisaFPGA board supported two hard-disk paths by routing the
Lisa parallel-port signals to either an external real ProFile connector or the
onboard ESP32-based ESProFile emulator. The ProFile emulator was ESP32 firmware,
not FPGA Verilog.

The MiSTer port now uses an internal SystemVerilog ProFile emulator
(`rtl/profile.sv`) backed by MiSTer/HPS block-device signals. Verilator uses
`verilator/sim/sim_blkdevice.cpp` as the host-side block-device service.

## Done Recently

- Enabled 2MB RAM in the simulator so Lisa Office System can load past the
  earlier loader memory-exhaustion failure (`10727`).
- Fixed the Verilator block-device mount delay so `img_mounted` clears and later
  ProFile sector requests are not blocked.
- Reworked the Verilator block-device service to stream 256 16-bit words per
  sector and hold `sd_ack` until `sd_rd`/`sd_wr` deasserts.
- Fixed the block-device cycle counter width. ProFile boot begins after billions
  of simulator cycles in the 2MB configuration, so an `int` cycle argument
  overflowed and made the backend ignore late `sd_rd` requests.
- Cloned ESProFile into `reference/ESProFile` for protocol comparison.
- Matched ESProFile's 10MB spare-table identity fields for `10350592` byte
  images by passing `img_size` into `profile.sv`.
- Matched ESProFile read timing more closely by preloading the first status byte
  before releasing `_BSY` for a ProFile read response.
- Reviewed LisaEm's ProFile implementation. LisaEm normalizes ProFile media as
  DC42 and applies `deinterleave5()` before reading that image; the local raw
  `.image` files are already in physical ProFile order, so the RTL should not
  apply that mapping to these files.
- Identified Lisa ROM boot error `84` as `BADHDR`, then verified the current
  ProFile path delivers block 0 correctly (`hdr0=00000022aaaa8200`).
- Verified the LOS 3.0 image is already in physical ProFile interleave order:
  `LDPROF`'s 9:1 software interleave maps the logical boot blocks to valid
  checksummed physical blocks.
- Verified clean simulator video through the ROM self-check window.
- Added headless diagnostics: `--status-start`, `--stop-pc`, `--stop-start`, and
  `--dump-rom-state`.

## Active ProFile Boot Work

1. Run a long headless boot with:

   ```sh
   cd verilator
   ./obj_dir/Vemu --headless --boot-profile \
     --profile "Lisa Office System 3.0 and Workshop 3.0.image" \
     --cycles 3800000000 \
     --status-interval 500000000 \
     --screenshot /tmp/lisa.ppm
   ```

2. Watch the ProFile status fields:

   - `PRO{state=1b}` means `profile.sv` is in `ST_HPS_READ`.
   - `sd_rd=001` with no `rdack` means the block-device backend or `sd_ack`
     handoff is still suspect.
   - advancing `rdack` means the sector read completed and the next issue is
     likely ProFile protocol/status/data behavior.
   - `stat0` and `hdr0` capture the first status bytes and first eight block-0
     header bytes delivered by `profile.sv`; for the LOS 3.0 image, a correct
     first header starts `00000022aaaa8200`.

3. Current traces leave ROM block-0 validation, execute loaded ProFile boot code
   around `0x207F34..0x207F38`, then return to ROM/monitor code. Use
   `--stop-pc 0xFE0084 --stop-start 3300000000` to catch a loader `bootbomb`
   jump into the ROM monitor before registers are clobbered.

4. A fresh run is validating the ESProFile-style status-byte preload fix. If it
   still returns to ROM/monitor code, the next suspect is later loaded-driver
   ProFile status/handshake timing rather than block-0 data or spare-table
   identity.

## Hardware / Release Follow-Ups

- Test the current ProFile changes on FPGA once the simulator reaches a useful
  checkpoint or the user can try a build.
- Strip or gate any remaining debug-only probes before a clean release build.
- Close remaining timing warnings, especially SCC/FPU paths that still run from
  raw `clk_sys`.
- Restore SCC baud-rate correctness by adding proper clock enables to
  `z8530_scc`.
- Decide whether the ESProFile reference clone should stay untracked under
  `reference/` or be documented as a local-only reference checkout.
