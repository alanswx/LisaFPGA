# Lisa Verilator Simulator

This directory contains the Verilator/SDL simulator wrapper for the MiSTer Lisa
core.

## Build

Install Verilator, SDL2, and a C++ compiler, then build from this directory:

```sh
make
```

The executable is written to `obj_dir/Vemu`.

## Run

Run from this directory so the ROM memory symlinks and local disk images resolve:

```sh
./obj_dir/Vemu
```

By default the simulator tries to mount `profile.image` from this directory.
Disk images are local runtime media and are not tracked. Use `--profile` to
select another ProFile image:

```sh
./obj_dir/Vemu --profile /path/to/profile.image
```

For command-line runs without SDL/ImGui, use `--headless`. `--cycles` limits the
run; `--cycles 0` runs until interrupted. `--screenshot` writes the final frame
as a PPM file. `--status-interval` controls the diagnostic status cadence, and
`--status-start` suppresses periodic status before a cycle count. For targeted
debugging, `--stop-pc <addr>` stops when the 68k reaches an address after
`--stop-start <count>`, and `--dump-rom-state` prints simulator RAM/register
diagnostics at exit.

```sh
./obj_dir/Vemu --headless \
  --profile "Lisa Office System 3.0 and Workshop 3.0.image" \
  --cycles 3800000000 \
  --status-start 3450000000 \
  --status-interval 500000000 \
  --screenshot /tmp/lisa.ppm
```

`--boot-profile` is a headless helper for ProFile boot testing. It asks the ROM
for the `STARTUP FROM` menu during the ROM keyboard-scan window and then injects
the ProFile boot key sequence. In the current 2MB RAM configuration, the ROM can
still spend a long time in memory self-check before that scan point. Add
`--skip-ram-test` to retain ROM memory sizing and the initial low-memory check
while bypassing the subsequent full-memory sweep. The simulator applies this
patch only after the ROM checksum has completed; it does not alter the ROM files
or FPGA behavior.

`--crash-trace` arms after execution transfers from ROM into the loaded ProFile
boot code. It keeps the last 256 distinct instruction PCs and stops on an
invalid 32-bit PC, HALT/double fault, or CPU-board reset. Address-error pulses
are reported but are not terminal because the fx68k core also exposes transient
pulses during valid byte cycles. The dump includes bus signals, registers,
selected MMU state, and ROM/loader scratch RAM before restart diagnostics can
clear them.

## ProFile Emulation

The original Xilinx LisaFPGA board did not implement the ProFile disk emulator in
FPGA Verilog. It muxed the Lisa parallel-port signals between:

- the onboard ESP32-based ESProFile emulator, and
- the external real ProFile connector.

The MiSTer port instead has an internal SystemVerilog ProFile protocol emulator
in `../rtl/profile.sv`. On MiSTer hardware it uses the normal HPS block-device
signals (`sd_lba`, `sd_rd`, `sd_wr`, `sd_ack`, `sd_buff_*`). In Verilator,
`sim/sim_blkdevice.cpp` is the host-side stand-in for that HPS block-device
service.

The internal emulator follows ESProFile behavior where it matters for booting:
read/write command responses, 532-byte ProFile blocks, the spare-table read, and
5MB/10MB spare-table identity fields derived from the mounted image size.
The read path also preloads the first status byte before releasing `_BSY`,
matching ESProFile's `sendData(blockData[0]); clearBSY(); sendMultiData()`
ordering so the Lisa never sees the previous command-response byte as status.

LisaEm is useful as a second protocol reference, but it uses a normalized DC42
image path: its ProFile state machine receives the Lisa's drive-level block
number and applies `deinterleave5()` before reading the DC42 sector. The local
`.image` files used by this simulator are raw 532-byte ProFile blocks. The LOS
3.0 test image is already in physical ProFile interleave order, so the RTL does
not apply LisaEm's `deinterleave5()` mapping to raw image reads.

## Current Boot Notes

The simulator is configured for 2MB RAM (`RAM_SEL=2'b11`) so Lisa Office System
has enough memory to load. Earlier 512KB runs reached Lisa OS error `10727`,
which maps to loader memory exhaustion.

The ProFile boot path now passes the transition after ROM self-check:

- the image mount handshake now completes and clears `img_mounted`;
- the simulator block backend now services `sd_rd`/`sd_wr` deterministically and
  holds `sd_ack` until the request drops;
- the block backend now uses a 64-bit cycle counter; the old `int` argument
  overflowed before the late ProFile boot request in long 2MB runs;
- 10MB ProFile images now report the same spare-table size/device fields that
  ESProFile reports for a `10350592` byte image.
- the ProFile read state now has the first status byte on the parallel bus
  before `_BSY` is released, matching ESProFile's command/data timing.

Lisa ROM boot error `84` is `BADHDR`. It means the ROM read block 0 far enough
to inspect the 20-byte ProFile header, but the file ID at header offset 4 was not
`0xAAAA`. For the local LOS 3.0 image, the first block starts
`00 00 00 22 aa aa 82 00`; the simulator status field `hdr0` captures what the
emulated ProFile actually delivered. Current long-run traces show
`hdr0=00000022aaaa8200`, so the earlier error-84 signature is fixed; the
LOS loader executes from RAM. A later debugger/reboot was caused by the MMU
A12:A9 latch sampling the SLR adder result as `_MALE` rose, rather than retaining
the preceding SOR result. That copied the OS MMU utility to physical `0x001600`
while its segment mapped `0x000800`. The CPU-board latch now captures during the
SOR phase. With that fix, the utility is installed at `0x000800` and the simulator
runs without reset, halt, or invalid PC through 850,000,000 cycles/frame 626.
The screen is still at the boot hourglass at that point, so full desktop startup
is not yet proven. The LOS 3.0 image is already in physical ProFile interleave
order; `LDPROF`'s 9:1 software interleave maps logical boot blocks to valid
checksummed physical blocks.

Long headless runs are expected. The ROM self-check can still be in progress at
hundreds of frames.

## Notes

`../rtl/t420_notri.v` is a generated Verilog translation of the COP421 VHDL
model. Verilator builds use that file because the rest of the FPGA build still
uses `../rtl/t420_notri.vhd`.
