# MiSTer RTC to Lisa Clock Integration

## Purpose

MiSTer can supply the host's current date and time to a core. The Lisa core can
use this value to initialize the Lisa clock at startup, but it should do so by
programming the real COP421 clock through the normal Lisa COPS command protocol.

This document records the current design findings and a proposed implementation.
No RTL changes described here have been made yet.

## Existing MiSTer Interface

`sys/hps_io.sv` exposes two time values:

```systemverilog
// RTC MSM6242B layout
output reg [64:0] RTC,

// Seconds since 1970-01-01 00:00:00
output reg [32:0] TIMESTAMP,
```

Bit 64 of `RTC` toggles when a new RTC value arrives. Bit 32 of `TIMESTAMP`
does the same for a new timestamp. The current `hps_io` instance in `Lisa.sv`
does not connect either output.

`RTC` is the preferable source for this integration because MiSTer supplies it
in local civil time. Using `TIMESTAMP` would require a timezone conversion that
the core cannot reliably infer.

Before implementation, verify the exact MSM6242B digit positions against the
MiSTer framework version used by this repository. The conversion logic will
need the year, month, day, hour, minute, and second fields.

## Lisa Clock Ownership and Format

The Lisa has no independent motherboard RTC block. Its always-running COP421
firmware owns the clock/calendar in the COP's internal data RAM. The Lisa OS
accesses it through COPS commands.

The Lisa clock value is a 48-bit packet:

```text
0000yyyy dddddddd ddddhhhh hhhhmmmm mmmmssss sssstttt
```

- `yyyy`: binary year offset, where 1980 is zero
- `dddddddddddd`: BCD day of year
- `hhhhhhhh`: BCD hour
- `mmmmmmmm`: BCD minute
- `ssssssss`: BCD second
- `tttt`: BCD tenths of a second

The clock rolls over every 16 years. A host year must therefore be encoded as:

```text
(host_year - 1980) modulo 16
```

For example, 2026 is represented by the Lisa as year slot 14, corresponding to
1994 in the original 1980-1995 Lisa epoch. This limitation is inherent in the
Lisa clock format; software that assumes only the original epoch will still see
1994 rather than 2026.

The MiSTer month/day value must be converted to a BCD day of year. Leap-year
handling must use the actual host year when calculating that day, before the
year is reduced modulo 16. Tenths may be initialized to zero because MiSTer's
RTC interface has one-second resolution.

## COPS Clock Protocol

The relevant commands are:

| Command | Meaning |
| --- | --- |
| `0x02` | Read the clock |
| `0x1n` | Write one clock nibble, where `n` is the data nibble |
| `0x2C` | Disable clock/timer and enter clock-set mode |
| `0x25` | Enable clock and disable timer |

The Lisa OS `SetClock` routine sends this exact sequence:

1. Send `0x2C`.
2. Send all 16 clock/alarm nibbles as sixteen `0x10 | nibble` commands.
3. Send `0x25`.

Although the visible clock packet is 12 nibbles, the hardware transaction is
16 nibbles because the COPS storage also includes the alarm field. The proposed
sequencer must reproduce the OS routine's ordering exactly, including zeroing
the alarm nibbles. `Lisa_Source/LISA_OS/LIBHW/libhw-TIMERS.TEXT.unix.txt`
contains the authoritative packet description and `SetClock` implementation.

## Recommended Wiring

### 1. `Lisa.sv`: receive and convert MiSTer RTC

Connect the `RTC` output on the existing `hps_io` instance. Add conversion logic
or instantiate a small dedicated module that:

- detects a change of the `RTC[64]` update toggle;
- decodes the MSM6242B digits;
- validates the date and time digits;
- calculates the BCD Lisa day of year;
- packs the Lisa clock and four zero alarm nibbles; and
- raises a one-shot clock-load request.

Invalid or absent host RTC data should not prevent the Lisa from powering on.

### 2. `rtl/top.sv`: carry the request into the I/O board

Add ports for the packed clock data, load request, completion indication, and
optionally a failure/timeout indication. Keep conversion policy above the I/O
board; the I/O board should only implement the COPS transaction.

### 3. `rtl/IO_board.sv`: program the real COP

Add a small startup command sequencer at the existing VIA-to-COP L-bus boundary.
While it owns the bus, it should mux its command byte in place of
`L_COP_out_int`, use the existing `_READY_COP` handshake, and issue:

```text
2C, 1n x 16, 25
```

The relevant existing signals are:

- `L_COP_out_int`: keyboard VIA Port A command data
- `L_COP_out`: conditioned L-bus value connected to COP `io_l_i`
- `_READY_COP`: COP indication that it can accept a command
- `READ_ACK_COP`: acknowledgement for bytes flowing from COP to VIA
- `DATA_QUEUED_COP`: COP indication that response data is available

The sequencer should follow the timing already enforced by the conditioned VIA
DDRA/L-bus logic. It must not merely pulse a command for one system clock. The
COP is sensitive to how long the command remains driven around `_READY_COP`.

The RTC loader should own the bus only while the Lisa is still logically powered
off, so it cannot collide with ROM or OS COPS traffic.

### 4. Delay automatic power-on until initialization finishes

`Lisa.sv` currently generates an automatic power-button press after COP power-on
reset has had time to finish. Gate that press until one of these occurs:

- the RTC transaction completes successfully;
- no valid MiSTer RTC value arrives within a bounded wait; or
- the RTC transaction itself times out.

This ordering lets the real COP begin keeping time before the boot ROM or OS can
ask for the clock. The timeout is required so a missing or malformed RTC update
can never block boot.

## Initialization Policy

Seed the COP once during core startup. Do not continuously resynchronize it.

Periodic writes could race Lisa software, overwrite a user-set time or alarm,
and introduce discontinuities while the OS reads the clock. The real COP should
remain the clock owner after startup, including while the Lisa is soft-powered
off but the FPGA core remains active.

A later explicit reset policy can decide whether a MiSTer reset should reseed
the clock. The safest initial behavior is one load per FPGA core reset.

## Approaches to Avoid

### Direct writes to COP internal RAM

Do not reach into generated `rtl/t420_notri.v` hierarchy and modify the COP data
RAM directly. The firmware's internal addresses and bookkeeping are not a
stable interface, and direct writes can leave firmware state inconsistent.

### Synthesized clock-read replies

Do not intercept `0x02` and fabricate clock replies outside the COP. That would
split FPGA and simulation behavior and bypass the firmware being validated.
Programming the actual COP keeps one implementation for both targets.

### Simulation-only initialization

Do not put the RTC loader behind `SIMULATION` or implement it in the Verilator
harness. Simulation may provide test RTC digits as stimulus, but the conversion,
sequencing, handshaking, and timeout logic must be the same synthesizable RTL
used on the FPGA.

## Suggested Verification

1. Unit-test month/day to day-of-year conversion, including leap years and the
   December 31 boundary.
2. Verify year modulo-16 encoding, including 2026 producing year slot 14.
3. Trace the real COP bus and confirm `0x2C`, sixteen `0x1n` bytes, then `0x25`.
4. Boot with no RTC update and confirm the timeout still permits power-on.
5. Boot with invalid BCD digits and confirm the value is rejected without
   delaying boot indefinitely.
6. Issue the normal Lisa `0x02` clock-read command and verify the COP returns the
   initialized value and advances it over simulated time.
7. Run the normal real-COP boot simulation through the previous frame-410 crash
   boundary to ensure the loader does not alter keyboard, power, or COPS timing.
8. Confirm synthesis does not introduce cross-clock warnings at the RTC update
   toggle or at the sequencer request/completion signals.

## Relevant Files

- `Lisa.sv`: MiSTer `hps_io` instance and automatic power-button logic
- `sys/hps_io.sv`: MiSTer `RTC` and `TIMESTAMP` interfaces
- `rtl/top.sv`: wrapper path between `Lisa.sv` and the I/O board
- `rtl/IO_board.sv`: COP instance, L bus, `_READY_COP`, and VIA conditioning
- `rtl/t420_notri.v`: generated real COP421 implementation; do not modify its RAM
- `Lisa_Source/LISA_OS/LIBHW/libhw-TIMERS.TEXT.unix.txt`: Lisa clock packet and
  `SetClock` command sequence
- `reference/lisaem/src/include/cops.h`: LisaEm COPS command definitions

