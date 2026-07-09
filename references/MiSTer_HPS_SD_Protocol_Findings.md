# MiSTer HPS sd_buff Protocol — Findings & Sim/FPGA Alignment

**Date:** 2026-07-09
**Context:** ProFile boot works in the Verilator sim but fails on the DE10-Nano
with boot error **84 (BADHDR)**. Root cause is a mismatch between the sim's
block-device model and the real MiSTer HPS `sd_buff` protocol. This doc records
the finding and specifies the real protocol so the sim can be aligned — the goal
being **"if it works in sim, it works on the FPGA."**

---

## 1. Symptom

- Keyboard fixed (COP clock was 2× too slow — separate fix, committed). The Lisa
  now gets through the keyboard handshake and **attempts the ProFile boot**.
- On hardware it reads block 0, but the delivered block-0 header is **all zeros**
  → FILEID (bytes 4-5) = `0x0000` ≠ `0xAAAA` → **BADHDR (84)**.
- The SD image is correct: `Lisa Office System 3.0 and Workshop 3.0.img` block 0
  = `00 00 00 22 aa aa 82 00`.
- The Verilator sim delivers the same block 0 **correctly** and passes BADHDR.

## 2. Hardware measurement (JTAG ISSP probe added to profile.sv)

Read of one boot attempt on the FPGA:

| signal | value | meaning |
|---|---|---|
| `sd_lba` | `0` | requested sector 0 (correct) |
| `rd_acks` (`sd_rd & sd_ack` rises) | `1` | HPS *did* assert `sd_ack` |
| `sd_wr_cnt` (`sd_buff_wr` pulses) | **`0`** | **HPS never streamed a single word into the cache** |
| `dbg_rd_data_cnt` | `24` | emulator delivered 24 bytes (of zeros) to the Lisa |
| `dbg_block0_hdr` | `0x0000…` | delivered header = all zeros |

So: `sd_ack` fires, but **no `sd_buff_wr` ever fires** → the cache stays zero →
the emulator marks the sector "valid" (empty) and delivers zeros.

## 3. Root cause — sd_ack timing is modeled backwards in the sim

### Real MiSTer HPS protocol (from `sys/hps_io.sv`)

For a sector **read** (core → wants data from disk):

1. Core asserts `sd_rd[slot] = 1` (level, held).
2. HPS reads the sector into its buffer.
3. HPS **asserts `sd_ack[slot] = 1`** (hps_io cmd `0x18`).
4. **While `sd_ack` stays HIGH**, HPS streams the buffer: for each word it drives
   `sd_buff_addr`, `sd_buff_dout`, and pulses `sd_buff_wr = 1` (256 words for a
   512-byte sector in WIDE/16-bit mode).
5. HPS **deasserts `sd_ack[slot] = 0`**.
6. Core deasserts `sd_rd`. The core captured the data via `sd_buff_wr` **during**
   `sd_ack = 1`, and should treat the **falling edge of `sd_ack`** (or "all words
   received") as "transfer complete".

**Key invariant: `sd_ack` is HIGH for the entire duration of the `sd_buff_wr`
stream. Data and ack are concurrent.**

Writes are symmetric: `sd_wr` held, `sd_ack` high during the stream, HPS drives
`sd_buff_addr` and reads the core's registered `sd_buff_din` for each word, then
`sd_ack` low.

### Sim protocol (current `verilator/sim/sim_blkdevice.cpp`)

1. Sees `sd_rd`, seeks, sets a 1200-cycle `ack_delay` — `sd_ack` held **LOW**.
2. Streams words: for `bytecnt` 0..255 sets `sd_buff_dout`/`sd_buff_addr`, pulses
   `sd_buff_wr = 1`, and **`bitclear(sd_ack)` — `sd_ack` stays LOW during the
   whole stream.**
3. **After** all 256 words: `bitset(sd_ack)` — raises `sd_ack` HIGH as a single
   *end-of-transfer* signal.

So in the sim, **`sd_buff_wr` fires while `sd_ack = 0`, and `sd_ack` only goes
HIGH once, after the data is already delivered.** This is the opposite phase from
the real HPS.

### Why code passes in sim but fails on FPGA

`rtl/profile.sv` `ST_HPS_READ` (and `ST_HPS_WRITE`) does:

```systemverilog
ST_HPS_READ: begin
    sd_rd <= 1'b1;
    if (sd_ack) begin          // <-- acts on sd_ack HIGH
        sd_rd <= 1'b0;
        cache_secN_valid <= 1'b1;   // mark valid
        state <= return_state;      // and move on
    end
end
```

- **In sim:** `sd_ack` goes high *after* streaming, so when this fires the cache
  is already loaded → marking valid + proceeding is correct.
- **On real HPS:** `sd_ack` goes high *at the start* of streaming. This fires
  immediately, deasserts `sd_rd`, and moves on **before any word is captured** —
  and dropping `sd_rd` mid-transfer can abort the HPS stream. Result: `sd_buff_wr`
  never lands, cache is zero → BADHDR.

## 4. Fix — make the sim match the real HPS, then write the core to the real protocol

### 4a. sim_blkdevice.cpp (align to real MiSTer)

Model `sd_ack` as **HIGH for the whole transfer**, concurrent with `sd_buff_wr`:

- On seeing `sd_rd`/`sd_wr`: after the read-latency delay, **assert `sd_ack = 1`**.
- Keep `sd_ack = 1` while pulsing `sd_buff_wr` for all 256 words (read), or while
  driving `sd_buff_addr` and sampling `sd_buff_din` for all 256 words (write).
  Note the 1-cycle registered-read latency on `sd_buff_din` for writes.
- After the last word, **deassert `sd_ack = 0`**.
- Do **not** raise `sd_ack` only at the end.

### 4b. profile.sv HPS_READ / HPS_WRITE (write to the real protocol)

Capture during `sd_ack`, and complete on the **falling edge** of `sd_ack` — and
guard against a stale/early ack so a real acked transfer is required:

```systemverilog
// one flag + ack history
reg hps_acked; reg ack_d;
ack_d <= sd_ack;
ST_HPS_READ: begin
    if (~sd_ack & ~sd_rd & ~hps_acked) sd_rd <= 1'b1; // request only when bus idle
    if (sd_rd & sd_ack) begin sd_rd <= 1'b0; hps_acked <= 1'b1; end // our req acked
    if (hps_acked & ack_d & ~sd_ack) begin            // ack fell = stream complete
        hps_acked <= 1'b0;
        cache_secN_valid <= 1'b1;   // NOW the data is really in the cache
        state <= return_state;
    end
end
```

The cache write port (`if (sd_buff_wr) even_mem/odd_mem[…] <= sd_buff_dout`) already
captures during `sd_ack` regardless of FSM state — that part is correct and
unchanged. The change is: **don't mark valid / advance until the stream has
actually completed (ack fall), and don't act on a stale ack.**

This single protocol works identically in sim and on hardware, which is the
whole point: **sim results become predictive of FPGA behavior.**

## 4c. Why most cores DON'T hit this (rising- vs falling-edge completion)

"If the sim's `sd_ack` phase is wrong, why do IIgs/IIe/etc. work in sim?"
Because the ack *phase* only matters if you treat "`sd_ack` asserted" as
"transfer complete." The idiomatic MiSTer core does not:

```systemverilog
if (sd_ack) {sd_rd, sd_wr} <= 0;          // clear the request when acked — safe either way
if (sd_buff_wr) mem[sd_buff_addr] <= …;   // capture data during ack-high
if (old_ack && ~sd_ack) done <= 1;        // COMPLETE on the FALLING edge of ack
```

That completes only after the whole stream is over, so it is immune to whether
`sd_ack` is high *during* the stream (real HPS) or high *after* it (this sim).
Cores written this way pass in sim and on FPGA.

`profile.sv` instead conflates three actions into the first `sd_ack`: clear
`sd_rd`, mark the sector valid, and advance the FSM — all on `if (sd_ack)`. That
is only correct when the first `sd_ack` occurs *after* the data has already
landed, which is exactly (and only) what this project's custom
`sim_blkdevice.cpp` produces. The sim and the emulator were co-developed into a
self-consistent pair that does not match real-HPS timing. **The bug is
rising-edge completion, not the sim by itself** — but aligning the sim to the
real ack-during-stream phase (§4a) is what makes future sim work predictive, and
completing on ack-fall (§4b) is what makes the core robust regardless.

## 5. Verification hooks already in place

`rtl/profile.sv` exposes (Verilator `public_flat_rd` + JTAG ISSP probes
`LPRO`/`LPR2`, added this session):

- `dbg_block0_hdr` — bytes 0-7 delivered for block 0 (FILEID at bits [31:16]).
- `dbg_sd_fileid` — `sd_buff_dout` captured when writing slot-0 word 2 (the SD
  bytes for FILEID). **On a correct read this must read `0xAAAA`.**
- `dbg_sd_wr_cnt` (cache-load words), `rd_acks`, `dbg_rd_data_cnt`, `dbg_sd_lba_last`.

After aligning the protocol, on hardware expect: `sd_wr_cnt` ≈ 256/sector,
`dbg_sd_fileid = 0xAAAA`, `dbg_block0_hdr = 0x00000022_AAAA8200`, no error 84.
(These JTAG probes are debug-only — strip for release.)
