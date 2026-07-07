// Lisa.sv
// Top-Level wrapper module for Apple Lisa on MiSTer FPGA

`timescale 1 ps / 1 ps

module emu (
    // Master Clock and Reset
    input  wire        CLK_50M,
    input  wire  [1:0] RESET,

    // HPS Bus Interface (OSD, SD card, etc.)
    inout  wire [48:0] HPS_BUS,

    // Video Output (VGA / Analog)
    output wire        CLK_VIDEO,
    output wire        CE_PIXEL,
    output wire  [7:0] VGA_R,
    output wire  [7:0] VGA_G,
    output wire  [7:0] VGA_B,
    output wire        VGA_HS,
    output wire        VGA_VS,
    output wire        VGA_DE,
    output wire  [7:0] VGA_F1,
    output wire  [5:0] VGA_SL,
    output wire        VGA_SCALER,
    output wire        VGA_DISABLE,

    // Video Aspect Ratio / Scale controls
    output wire [12:0] VIDEO_ARX,
    output wire [12:0] VIDEO_ARY,
    output wire        HDMI_FREEZE,
    output wire        HDMI_BLACKOUT,
    output wire        HDMI_BOB_DEINT,

    // Audio Output
    input  wire        CLK_AUDIO,
    output wire [15:0] AUDIO_L,
    output wire [15:0] AUDIO_R,
    output wire        AUDIO_S,
    output wire        AUDIO_MIX,

    // SD Card / SPI Interface
    output wire        SD_SCK,
    output wire        SD_MOSI,
    input  wire        SD_MISO,
    output wire        SD_CS,
    input  wire        SD_CD,

    // SDRAM Interface (16-bit)
    output wire        SDRAM_CLK,
    output wire        SDRAM_CKE,
    output wire [12:0] SDRAM_A,
    output wire  [1:0] SDRAM_BA,
    inout  wire [15:0] SDRAM_DQ,
    output wire        SDRAM_DQML,
    output wire        SDRAM_DQMH,
    output wire        SDRAM_nCS,
    output wire        SDRAM_nCAS,
    output wire        SDRAM_nRAS,
    output wire        SDRAM_nWE,

    // User I/O / LEDs
    output wire        LED_USER,
    output wire        LED_POWER,
    output wire        LED_DISK,
    output wire  [1:0] BUTTONS,

    // Serial/UART Interface
    input  wire        UART_CTS,
    output wire        UART_RTS,
    input  wire        UART_RXD,
    output wire        UART_TXD,
    output wire        UART_DTR,
    input  wire        UART_DSR,

    input  wire  [6:0] USER_IN,
    output wire  [6:0] USER_OUT,

    input  wire        OSD_STATUS,

    // MiSTer DDRAM, ADC, and HDMI video info ports
    inout  wire  [3:0] ADC_BUS,
    output wire        DDRAM_CLK,
    output wire [28:0] DDRAM_ADDR,
    output wire  [1:0] DDRAM_BURSTCNT,
    input  wire        DDRAM_BUSY,
    input  wire [63:0] DDRAM_DOUT,
    input  wire        DDRAM_DOUT_READY,
    output wire        DDRAM_RD,
    output wire [63:0] DDRAM_DIN,
    output wire  [7:0] DDRAM_BE,
    output wire        DDRAM_WE,
    input  wire [11:0] HDMI_WIDTH,
    input  wire [11:0] HDMI_HEIGHT
);

    wire  [5:0] CONT_core;
    wire [15:0] D_SRAM;

    assign ADC_BUS  = 'Z;
    assign USER_OUT = '1;

    assign DDRAM_CLK = 1'b0;
    assign DDRAM_ADDR = 29'b0;
    assign DDRAM_BURSTCNT = 2'b0;
    assign DDRAM_RD = 1'b0;
    assign DDRAM_DIN = 64'b0;
    assign DDRAM_BE = 8'b0;
    assign DDRAM_WE = 1'b0;
    assign {SD_SCK, SD_MOSI, SD_CS} = 'Z;

    assign LED_DISK  = 0;
    assign LED_POWER = 0;
    assign BUTTONS   = 0;
    assign VGA_SCALER= 0;
    assign VGA_DISABLE = 0;
    assign HDMI_FREEZE = 0;
    assign HDMI_BLACKOUT = 0;
    assign HDMI_BOB_DEINT = 0;

    // Aspect Ratio Configuration (Lisa screen is approx 4:3)
    wire [1:0] ar = status[10:9];
    video_freak video_freak
    (
        .*,
        .VGA_DE_IN(VGA_DE_core),
        .VGA_DE(),
        .ARX((!ar) ? 12'd4 : (ar - 1'd1)),
        .ARY((!ar) ? 12'd3 : 12'd0),
        .CROP_SIZE(0),
        .CROP_OFF(0),
        .SCALE(status[12:11])
    );

    // OSD / Config String Definition
    `include "build_id.v"
    localparam CONF_STR = {
        "LISA;UART115200;",
        "-;",
        "S0,IMGVHD,Mount Hard Disk;",
        "-;",
        "O9A,Aspect ratio,4:3,Original,Full Screen,[ARC1];",
        "OBC,Scale,Normal,V-Integer,Narrower HV-Integer,Wider HV-Integer;",
        "-;",
        "O34,RAM Size,512KB,1MB,1.5MB,2MB;",
        "O56,CPU Speed,1x (5MHz),2x,3x,4x;",
        "O7,CPU ROM,H ROM,3A ROM;",
        "O8,I/O ROM,A8 ROM,40 ROM;",
        "ODE,Screen Color,Paper White,Green CRT,Amber CRT;",
        "-;",
        "R0,Reset & Apply;",
        "v,0;",
        "V,v",`BUILD_DATE
    };

    // OSD Control Status register
    wire [31:0] status;
    wire  [1:0] buttons;
    wire [10:0] ps2_key;
    wire [24:0] ps2_mouse;
    wire        dio_download;
    wire  [7:0] dio_index;
    wire        ioctl_write;
    wire [24:0] ioctl_addr;
    wire [15:0] ioctl_data;

    // ProFile Hard Disk HPS signals
    wire [31:0] sd_lba[1];
    wire        sd_rd;
    wire        sd_wr;
    wire        sd_ack;
    wire  [7:0] sd_buff_addr;
    wire [15:0] sd_buff_dout;
    wire [15:0] sd_buff_din[1];
    wire        sd_buff_wr;
    wire        img_mounted;
    wire [63:0] img_size;

    hps_io #(.CONF_STR(CONF_STR), .VDNUM(1), .WIDE(1)) hps_io
    (
        .clk_sys(clk_sys),
        .HPS_BUS(HPS_BUS),

        .buttons(buttons),
        .status(status),

        .sd_lba(sd_lba),
        .sd_rd(sd_rd),
        .sd_wr(sd_wr),
        .sd_ack(sd_ack),

        .sd_buff_addr(sd_buff_addr),
        .sd_buff_dout(sd_buff_dout),
        .sd_buff_din(sd_buff_din),
        .sd_buff_wr(sd_buff_wr),

        .img_mounted(img_mounted),
        .img_size(img_size),

        .ioctl_download(dio_download),
        .ioctl_index(dio_index),
        .ioctl_wr(ioctl_write),
        .ioctl_addr(ioctl_addr),
        .ioctl_dout(ioctl_data),
        .ioctl_wait(1'b0),

        .ps2_key(ps2_key),
        .ps2_mouse(ps2_mouse)
    );

    // Single core PLL: produces the 81.5 MHz master (clk_sys). Every other Lisa
    // clock is divided from clk_sys inside top/clock_divider/dotck_mmcm.
    // clk_mem (phase-shifted twin) is no longer used: the SDRAM controller
    // generates SDRAM_CLK itself via DDIO.
    wire clk_sys;
    wire clk_mem;
    wire usbclk_en;        // ~12MHz usbclk clock-enable strobe, generated inside top from clk_sys
    wire pll_locked;

    pll main_pll (
        .refclk(CLK_50M),
        .rst(1'b0),
        .outclk_0(clk_sys),  // 81.5 MHz system / master clock
        .outclk_1(clk_mem),  // 81.5 MHz phase-shifted clock for SDRAM
        .locked(pll_locked)
    );

    // Reset Generator
    reg n_reset = 0;
    always_ff @(posedge clk_sys) begin
        reg [15:0] rst_cnt;
        if (!pll_locked || status[0] || buttons[1] || RESET[0]) begin
            rst_cnt <= '1;
            n_reset <= 0;
        end else if (rst_cnt) begin
            rst_cnt <= rst_cnt - 1'b1;
        end else begin
            n_reset <= 1;
        end
    end

    // Video Output Mapping
    // CLK_VIDEO is the fast master; CE_PIXEL must strobe once per Lisa pixel
    // (DOTCK rate), otherwise the scaler samples 4x too fast and the HPS computes
    // a bogus (1x1) resolution. pixel_ce comes out of the core as dotck_en.
    wire pixel_ce;
    assign CLK_VIDEO = clk_sys;
    assign CE_PIXEL  = pixel_ce;

    wire _VSYNC_core;
    wire _HSYNC_core;
    wire VID_core;
    wire VGA_DE_core;

    // ---- Regenerated, self-consistent raster (HS + VS + DE from ONE ref) ----
    // The Lisa's raw _HSYNC/_VSYNC do NOT line up with the active-pixel region:
    // top.sv notes _HSYNC is "longer" than the true hsync and _clr_vid_clk is a
    // better proxy, and _VSYNC is "shorter" than the true vsync (VA_overflow is
    // the proxy). The old output drove VGA_HS/VS from the raw sync while
    // building DE from _clr_vid_clk — two inconsistent references. The scaler
    // resets its H counter on VGA_HS, which landed in the wrong place vs DE, so
    // it measured a bogus active width (screenshot ~35px) and the picture sat
    // off-centre. Here EVERYTHING is regenerated from a per-line dot counter
    // locked to the one reliable per-line marker: the rising edge of
    // _clr_vid_clk (dbg_clr), which fires once per line at the start of the
    // 45-word active fetch. Vertical uses ~VA_overflow (dbg_va).
    //
    // Live-tunable over the LVID ISSP source (16 bits):
    //   [10:0]  h_de_start  dot offset from line start to DE open (def 18)
    //   [15:11] (reserved)
    // so the picture can be centred without a rebuild.
    wire dbg_va, dbg_clr;
    wire [15:0] vid_src;
    // The Lisa's active video straddles the raw _HSYNC pulse (content sits at
    // ~[370..line_end)+[0..194), i.e. wrapped around hcnt=0). So we shift the
    // per-line origin ORIGIN_DELAY dots after _HSYNC — into the blanking GAP
    // between the two content halves — making the 720 active dots contiguous,
    // and regenerate a clean VGA_HS at that origin (in the blank). Both the
    // origin and the DE start are live-tunable via the LVID source so the
    // picture can be centred from screenshots without a rebuild:
    //   vid_src[10:0]  = h_de_start   (DE window start from origin; def 90)
    //   vid_src[15:11] = origin coarse (x16 dots; def 280 when 0)
    // Defaults (144, 20) were dialed in live from screenshots: they place the
    // origin in the content gap and centre the 720-wide picture.
    wire [10:0] h_de_start  = (vid_src[10:0] == 11'd0) ? 11'd20  : vid_src[10:0];
    wire [10:0] origin_delay= (vid_src[15:11] == 5'd0) ? 11'd144 : {vid_src[15:11], 4'b0};

    localparam [10:0] H_ACTIVE = 11'd720;  // active dots per line
    localparam [10:0] HS_WIDTH = 11'd64;   // regenerated HS pulse width

    // DE window only. HS/VS are the Lisa's RAW sync (passed through below) —
    // real pulses in the blanking that MiSTer's sync_fix + scandoubler
    // understand. (A regenerated HS confused that chain and made the scaler
    // measure an ~11px active width.)
    reg  [10:0] hcnt = 0;      // dots since line start (_HSYNC rising)
    reg  active_h = 0;
    reg  v_active = 0;
    // Vertical active = a COUNTER of exactly V_ACTIVE lines, anchored to the
    // first line where VA_overflow deasserts (active resume), clocked by _HSYNC
    // (which — unlike _clr_vid_clk — pulses on every line). VA_overflow does not
    // cleanly bound 364 lines (its raw extent gave ~443 active lines and leaked
    // DE into the blank, which is what video_freak mis-measured as the width).
    // A fixed line count guarantees exactly 364 clean active lines.
    // MEASURED signal behavior (per frame): _HSYNC = 380 edges (one clean pulse
    // per line), VA_overflow = 1 rise/1 fall, but _clr_vid_clk = 575 edges — it
    // is NOT a clean per-line signal (toggles ~1.5x/line), so resetting hcnt on
    // it chopped DE into extra pulses. So: horizontal line reference = _HSYNC
    // (reset hcnt at its rising edge), horizontal active = a 720-dot window at a
    // tunable offset h_de_start; vertical = a fixed 364-line counter clocked by
    // _HSYNC and anchored to VA_overflow's (clean) falling edge.
    // The FIRST active line after VA_overflow falls is a short/transition line
    // (~751 dots vs 895) — VA falls mid-line. video_freak/ascal measure that
    // first line, so we SKIP it: anchor the 363-line active window on the line
    // AFTER VA falls, so the first measured line is a full 895-dot line.
    localparam [9:0] V_ACTIVE = 10'd363;
    reg hs_de = 1;
    reg va_line = 1;                 // VA_overflow sampled at previous line
    reg va_fell = 0;                 // VA fell last line -> anchor on THIS one
    reg [9:0] vcnt = 10'd511;        // >=V_ACTIVE => vertically blanked
    reg [10:0] since_hs = 11'd2047;  // dots since raw _HSYNC rising
    reg hs_out = 1;                  // regenerated HS (active high pulse at origin)
    always_ff @(posedge clk_sys) if (pixel_ce) begin
        hs_de <= _HSYNC_core;
        if (!hs_de && _HSYNC_core) begin          // raw _HSYNC rising = line marker
            since_hs <= 11'd0;
            if (!dbg_va && va_line)               // VA just FELL: arm (skip this line)
                va_fell <= 1'b1;
            else if (va_fell) begin               // next line = first FULL active line
                va_fell <= 1'b0; vcnt <= 10'd0;
            end else if (vcnt < 10'd511)
                vcnt <= vcnt + 10'd1;
            va_line <= dbg_va;
        end else if (since_hs != 11'd2047) begin
            since_hs <= since_hs + 11'd1;
        end
        // Line origin = ORIGIN_DELAY dots after _HSYNC (in the content gap).
        if (since_hs == origin_delay) hcnt <= 11'd0;
        else                          hcnt <= hcnt + 11'd1;
        v_active <= (vcnt < V_ACTIVE);            // exactly 364 active lines
        active_h <= (hcnt >= h_de_start) && (hcnt < h_de_start + H_ACTIVE);
        hs_out   <= (hcnt < HS_WIDTH);            // regen HS at origin (in blank)
    end
    assign VGA_DE_core = active_h & v_active;

    // Regenerate VGA_VS: rise cleanly at a LINE BOUNDARY inside vertical blank.
    // VA_overflow marks the blank but rises mid-line, so using it (or the raw
    // _VSYNC) directly makes video_freak/ascal reset their line counter mid-
    // line and measure a partial line as the width. Instead: when VA_overflow
    // rises, arm; then assert VS on the NEXT _HSYNC edge (a real per-line marker
    // that — unlike _clr_vid_clk — keeps pulsing through the blank) and hold it
    // 3 lines. So VS rises between lines in the blank and the first active line
    // after it is a full 720-wide DE.
    reg hs_dv = 1, va_dv = 0, va_armed = 0, vs_out_r = 0;
    reg [1:0] vs_cnt = 0;
    always_ff @(posedge clk_sys) if (pixel_ce) begin
        hs_dv <= _HSYNC_core;
        va_dv <= dbg_va;
        if (dbg_va && !va_dv) va_armed <= 1'b1;        // VA_overflow rose -> arm
        if (!hs_dv && _HSYNC_core) begin               // _HSYNC rising = line boundary
            if (va_armed) begin
                vs_out_r <= 1'b1; vs_cnt <= 2'd0; va_armed <= 1'b0;
            end else if (vs_out_r) begin
                if (vs_cnt == 2'd2) vs_out_r <= 1'b0;
                else vs_cnt <= vs_cnt + 2'd1;
            end
        end
    end
    wire _VSYNC_out = vs_out_r;

    // DEBUG (ISSP "LVID"): comprehensive per-frame structure measurement,
    // latched at regenerated-VS rising. Stop guessing — measure the raw signals:
    //   LVID probe (32b): {first_w[11:0], n_lines[9:0], first_de_pos[9:0]}
    //   LVI2 probe (32b): {va_dots[13:0], vs_edges[5:0], de_falls[9:0], hglitch}
    //     first_w       = width of the first DE pulse after VS (video_freak's #)
    //     first_de_pos  = dots from VS-rising to that first DE pulse start
    //     n_lines       = DE pulses (active lines) counted between VS edges
    //     va_dots       = pixel_ce ticks with dbg_va (VA_overflow) high per frame
    //     vs_edges      = rising edges of _VSYNC_out per frame (should be 1)
    //     de_falls      = DE falling edges per frame (active lines, ~364)
    //     hglitch       = 1 if any DE pulse < 8 dots was seen this frame
    reg [11:0] hcpt = 0, first_w = 0;
    reg [9:0]  vcpt = 0, n_lines_lat = 0;
    reg [9:0]  de_pos = 0, first_de_pos = 0;
    reg [13:0] va_dots = 0, va_dots_lat = 0;
    reg [5:0]  vs_edges = 0, vs_edges_lat = 0;
    reg [9:0]  de_falls = 0, de_falls_lat = 0;
    reg        hglitch = 0, hglitch_lat = 0, got_first = 0;
    reg        de_p = 0, vs_p = 1;
    always_ff @(posedge clk_sys) if (pixel_ce) begin
        de_p <= VGA_DE_core;
        vs_p <= _VSYNC_out;
        de_pos <= de_pos + 10'd1;
        if (dbg_va) va_dots <= va_dots + 14'd1;
        if (!vs_p && _VSYNC_out) vs_edges <= vs_edges + 6'd1;
        if (!vs_p && _VSYNC_out) begin       // VS rising: latch previous frame + reset
            n_lines_lat  <= vcpt;
            va_dots_lat  <= va_dots;
            vs_edges_lat <= vs_edges;
            de_falls_lat <= de_falls;
            hglitch_lat  <= hglitch;
            vcpt <= 0; va_dots <= 0; vs_edges <= 0; de_falls <= 0;
            hglitch <= 0; got_first <= 0; de_pos <= 0;
        end
        if (VGA_DE_core) hcpt <= hcpt + 12'd1;
        if (!de_p && VGA_DE_core && !got_first) begin // first DE rising after VS
            first_de_pos <= de_pos; got_first <= 1'b1;
        end
        if (de_p && !VGA_DE_core) begin      // DE falling: end of an active line
            if (vcpt == 10'd0) first_w <= hcpt;
            if (hcpt < 12'd8) hglitch <= 1'b1;
            vcpt <= vcpt + 10'd1;
            de_falls <= de_falls + 10'd1;
            hcpt <= 12'd0;
        end
    end
    wire [31:0] vid_dbg  = { first_w, n_lines_lat, first_de_pos };
    // CONTENT + LINE-LENGTH measurement (in the origin-shifted hcnt domain):
    //   vid_l   = min hcnt where VID_core is active (left edge of picture)
    //   vid_r   = max hcnt where VID_core is active (right edge of picture)
    //   per_min = shortest line length (max hcnt before origin reset), min over
    //             frame — reveals if the first active line is short.
    //   per_max = longest line length.
    // Measured on active lines only (v_active). This tells me exactly where to
    // put the DE window and whether the first-line truncation is real.
    reg [10:0] vid_l = 11'd2047, vid_l_lat = 0;
    reg [10:0] vid_r = 0, vid_r_lat = 0;
    reg [10:0] per_min = 11'd2047, per_min_lat = 0;
    reg [10:0] per_max = 0, per_max_lat = 0;
    reg        vs_p2 = 1;
    always_ff @(posedge clk_sys) if (pixel_ce) begin
        vs_p2 <= _VSYNC_out;
        if (v_active && VID_core) begin
            if (hcnt < vid_l) vid_l <= hcnt;
            if (hcnt > vid_r) vid_r <= hcnt;
        end
        if (since_hs == origin_delay) begin   // origin reset = end of a line
            if (v_active) begin
                if (hcnt < per_min) per_min <= hcnt;   // hcnt just before reset = line len
                if (hcnt > per_max) per_max <= hcnt;
            end
        end
        if (!vs_p2 && _VSYNC_out) begin       // frame latch
            vid_l_lat <= vid_l; vid_l <= 11'd2047;
            vid_r_lat <= vid_r; vid_r <= 0;
            per_min_lat <= per_min; per_min <= 11'd2047;
            per_max_lat <= per_max; per_max <= 0;
        end
    end
    // LVI2 (secondary video-measurement probe) removed: video is solved and the
    // device is at routing capacity — dropping it prunes the vid_l/vid_r/per_min
    // frame-measurement chain to free ALMs/routing for the ProFile-debug probes.
    altsource_probe #(
        .sld_auto_instance_index ("YES"), .sld_instance_index (0),
        .instance_id ("LVID"), .probe_width (32), .source_width (16),
        .source_initial_value ("0"), .enable_metastability ("NO")
    ) u_vid_probe ( .source(vid_src), .probe(vid_dbg), .source_clk(clk_sys), .source_ena(1'b1) );

    // Color palette mapping (OSD selection supported)
    wire [7:0] gray = VID_core ? 8'hFF : 8'h00;
    wire [1:0] color_sel = status[14:13];
    reg [7:0] vga_r_val, vga_g_val, vga_b_val;
    always_comb begin
        case (color_sel)
            2'b01: begin // Green CRT
                vga_r_val = 8'h00;
                vga_g_val = gray;
                vga_b_val = 8'h00;
            end
            2'b10: begin // Amber CRT
                vga_r_val = gray;
                vga_g_val = {gray[7:1], 1'b0} + {gray[7:2], 2'b00}; // approx gray * 0.75
                vga_b_val = 8'h00;
            end
            default: begin // Paper White
                vga_r_val = gray;
                vga_g_val = gray;
                vga_b_val = gray;
            end
        endcase
    end
    assign VGA_R = vga_r_val;
    assign VGA_G = vga_g_val;
    assign VGA_B = vga_b_val;
    // HS = regenerated at the shifted line origin (in the blanking gap), so the
    // active video no longer straddles the sync. VS = regenerated from
    // VA_overflow, in the vertical blank.
    assign VGA_HS = hs_out;
    assign VGA_VS = _VSYNC_out;
    assign VGA_DE = VGA_DE_core;

    // Audio Output Conversion
    // Square wave output (TONE) combined with 3-bit volume attenuation (VC)
    wire TONE_core;
    wire [2:0] VC_core;
    reg [15:0] audio_sample;
    always_ff @(posedge clk_sys) begin
        // Volume levels: maps VC volume level to 15-bit amplitude
        reg [14:0] ampl;
        case (VC_core)
            3'd0: ampl <= 15'd0;
            3'd1: ampl <= 15'd2000;
            3'd2: ampl <= 15'd4000;
            3'd3: ampl <= 15'd6000;
            3'd4: ampl <= 15'd8000;
            3'd5: ampl <= 15'd12000;
            3'd6: ampl <= 15'd18000;
            3'd7: ampl <= 15'd24000;
        endcase
        audio_sample <= TONE_core ? {1'b0, ampl} : {1'b1, ~ampl + 1'b1};
    end
    assign AUDIO_L = audio_sample;
    assign AUDIO_R = audio_sample;
    assign AUDIO_S = 1'b0;
    assign AUDIO_MIX = 1'b0;

    // SDRAM Controller Interface
    wire _CE_SRAM;
    wire _OE_SRAM;
    wire _WE_SRAM;
    wire _UDS_SRAM;
    wire _LDS_SRAM;
    wire [20:1] A_SRAM;
    wire [15:0] DIN_SRAM;

    // The Lisa core (top) exchanges RAM data over the tri-state D_SRAM inout:
    // it drives write data onto D_SRAM during writes (SRAM_BUS_DIR=0 inside top)
    // and samples read data from D_SRAM during reads. So we must drive D_SRAM
    // with the SDRAM controller's read data whenever a read is in progress
    // (_OE_SRAM low), and take write data from D_SRAM. (This connection was
    // missing before: din/dout went to dangling local wires, so RAM writes
    // stored a constant and reads returned an undriven bus — the boot ROM's
    // memory sizing then found "no memory" and parked the CPU.)
    assign D_SRAM = _OE_SRAM ? 16'bZ : DIN_SRAM;

    // Bridge: one Lisa memory cycle becomes one req/ack transaction on port 0 of
    // the controller. _CE_SRAM falls one clk after RAS+CAS are both active, at
    // which point address, strobes and (early-write) data are all stable. A Lisa
    // cycle lasts >=400ns while the controller round-trip is ~90ns (~180ns if it
    // collides with a refresh), so read data is back well before the core uses it.
    // Reads (including all video fetches) issue immediately at cycle start so the
    // data is back with maximum margin. Writes wait until the 68k byte strobes
    // are actually asserted (they can lag CAS on a write cycle), matching the
    // level-sensitive WE behavior of the original DRAM array.
    reg         lisa_req = 0;
    wire        lisa_ack;
    reg  [24:1] lisa_addr;
    reg  [15:0] lisa_din;
    reg         lisa_wrl, lisa_wrh;
    reg         ce_sram_d = 1;
    reg         wr_pending = 0;
    reg  [11:0] lisa_rd_cnt = 0, lisa_wr_cnt = 0; // DEBUG: activity counters for LRAM probe
    always_ff @(posedge clk_sys) begin
        ce_sram_d <= _CE_SRAM;
        if (ce_sram_d && !_CE_SRAM) begin
            // Lisa memory cycle begins (RAS+CAS both active, address stable)
            if (!_OE_SRAM) begin // read cycle: issue now
                lisa_addr <= {4'b0000, A_SRAM};
                lisa_wrl  <= 1'b0;
                lisa_wrh  <= 1'b0;
                lisa_req  <= ~lisa_req;
                lisa_rd_cnt <= lisa_rd_cnt + 1'd1;
            end else begin // write cycle: wait for the data strobes
                wr_pending <= 1'b1;
            end
        end else if (_CE_SRAM) begin
            wr_pending <= 1'b0;
        end else if (wr_pending && (!_LDS_SRAM || !_UDS_SRAM)) begin
            lisa_addr <= {4'b0000, A_SRAM};
            lisa_din  <= D_SRAM; // top drives write data onto D_SRAM during writes
            lisa_wrl  <= ~_LDS_SRAM;
            lisa_wrh  <= ~_UDS_SRAM;
            lisa_req  <= ~lisa_req;
            wr_pending <= 1'b0;
            lisa_wr_cnt <= lisa_wr_cnt + 1'd1;
        end
    end

    // DEBUG (bring-up ISSP "LRAM"): SDRAM self-test on controller port 1.
    // After PLL lock, write two signature words, then re-read them every ~0.4s
    // (the re-read also proves refresh/retention). Expected on the probe:
    // st_rd0=0xA5C3, st_rd1=0x5A3C, st_loops incrementing.
    reg         st_req = 0;
    wire        st_ack;
    reg  [24:1] st_addr;
    reg  [15:0] st_din;
    reg         st_wr;
    wire [15:0] st_dout;
    reg  [3:0]  st_state = 0;
    reg  [15:0] st_rd0 = 0, st_rd1 = 0;
    reg  [3:0]  st_loops = 0;
    reg  [24:0] st_timer = 0;
    always_ff @(posedge clk_sys) begin
        if (!pll_locked) begin
            st_state <= 0;
            st_timer <= 0;
        end else case (st_state)
            0: begin st_addr <= 24'h01234; st_din <= 16'hA5C3; st_wr <= 1; st_req <= ~st_req; st_state <= 1; end
            1: if (st_ack == st_req) st_state <= 2;
            2: begin st_addr <= 24'h04321; st_din <= 16'h5A3C; st_wr <= 1; st_req <= ~st_req; st_state <= 3; end
            3: if (st_ack == st_req) st_state <= 4;
            4: begin st_addr <= 24'h01234; st_wr <= 0; st_req <= ~st_req; st_state <= 5; end
            5: if (st_ack == st_req) begin st_rd0 <= st_dout; st_state <= 6; end
            6: begin st_addr <= 24'h04321; st_wr <= 0; st_req <= ~st_req; st_state <= 7; end
            7: if (st_ack == st_req) begin st_rd1 <= st_dout; st_loops <= st_loops + 1'd1; st_state <= 8; end
            8: begin // idle ~0.4s, then re-read (retention/refresh check)
                st_timer <= st_timer + 1'd1;
                if (&st_timer) st_state <= 4;
            end
            default: st_state <= 0;
        endcase
    end

    wire [63:0] ram_dbg = { st_rd1, st_rd0, st_loops, st_state, lisa_wr_cnt, lisa_rd_cnt };
    altsource_probe #(
        .sld_auto_instance_index ("YES"), .sld_instance_index (0),
        .instance_id ("LRAM"), .probe_width (64), .source_width (1),
        .source_initial_value ("0"), .enable_metastability ("NO")
    ) u_ram_probe ( .source(), .probe(ram_dbg), .source_clk(clk_sys), .source_ena(1'b1) );

    // (Removed the LBUS write/read-back checker — SDRAM path confirmed good.)

    // Sorgelig's proven MiSTer SDRAM controller. It generates SDRAM_CLK itself
    // (DDIO, 180 degrees from clk_sys), so the phase-shifted clk_mem PLL output
    // is no longer used.
    sdram_ctrl sdram_i (
        .SDRAM_DQ(SDRAM_DQ),
        .SDRAM_A(SDRAM_A),
        .SDRAM_DQML(SDRAM_DQML),
        .SDRAM_DQMH(SDRAM_DQMH),
        .SDRAM_BA(SDRAM_BA),
        .SDRAM_nCS(SDRAM_nCS),
        .SDRAM_nWE(SDRAM_nWE),
        .SDRAM_nRAS(SDRAM_nRAS),
        .SDRAM_nCAS(SDRAM_nCAS),
        .SDRAM_CLK(SDRAM_CLK),
        .SDRAM_CKE(SDRAM_CKE),

        .init(!pll_locked),
        .clk(clk_sys),

        .addr0(lisa_addr), .wrl0(lisa_wrl), .wrh0(lisa_wrh),
        .din0(lisa_din), .dout0(DIN_SRAM), .req0(lisa_req), .ack0(lisa_ack),

        .addr1(st_addr), .wrl1(st_wr), .wrh1(st_wr),
        .din1(st_din), .dout1(st_dout), .req1(st_req), .ack1(st_ack),

        .addr2(24'd0), .wrl2(1'b0), .wrh2(1'b0),
        .din2(16'd0), .dout2(), .req2(1'b0), .ack2()
    );

    // Keyboard Adaptor
    wire [7:0] hid_modifiers;
    wire [7:0] hid_key_code;
    wire       hid_report;

    ps2_to_usb_hid ps2_to_hid_i (
        .clk(clk_sys),
        .reset(!n_reset),
        .ps2_key(ps2_key),
        .key_modifiers(hid_modifiers),
        .key_code(hid_key_code),
        .report(hid_report)
    );

    wire kbd_serial_wire;
    wire kbd_out_sig;

    // hid_report is a single-clk_sys pulse but the adapter samples report only
    // on usbclk_en ticks (1-in-~7), dropping most key events. Hold the event
    // until the adapter's next tick consumes it.
    reg kbd_report_pend = 0;
    always @(posedge clk_sys) begin
        if (usbclk_en && kbd_report_pend) kbd_report_pend <= 1'b0;
        if (hid_report) kbd_report_pend <= 1'b1;
    end

    usb_keyboard_interface kbd_adapter_i (
        .clk_sys(clk_sys),
        .usbclk_en(usbclk_en), // usb keyboard adapter runs on clk_sys paced by usbclk_en
        .usbrst(n_reset),
        .key_modifiers_in(hid_modifiers),
        .key1_in(hid_key_code),
        .report(kbd_report_pend),
        .KBD_in(kbd_serial_wire),
        .KBD_out(kbd_out_sig)
    );

    // Open-collector keyboard line as an explicit wired-AND (idle high): the
    // Lisa side (COP/VIA via top) and our USB keyboard adapter each pull it low.
    // (Was a tri-state z-net, which Quartus resolved to a constant 0 — the COP
    // saw a permanently jammed keyboard line.)
    wire kbd_line_out_top;
    // DEBUG: kbd_mute (LKBD source bit 0) forces the adapter's keyboard-line
    // output idle-high, so the COP sees no keyboard traffic. The Lisa's boot
    // scan reads any key-DOWNSTROKE (bit7 set) as "a key was hit" and shows the
    // STARTUP FROM menu instead of auto-booting the ProFile — and our adapter's
    // 0x80 reset ID = downstroke of key 0. Mute it to test ProFile auto-boot.
    // NOTE: the STARTUP-FROM menu is NOT caused by the adapter's reset ID —
    // muting the adapter still leaves the COP presenting 0x80 (the keyboard
    // reset code) to the Lisa, and the boot scan still pops the menu. The 0x80
    // originates in the COP's reset handling and races the ROM's RSTSCAN. The
    // JTAG kbd_mute is kept only as a debug knob (LKBD source bit 0).
    wire kbd_mute;
    wire kbd_out_eff = kbd_mute ? 1'b1 : kbd_out_sig;
    assign kbd_serial_wire = kbd_line_out_top & kbd_out_eff;

    // DEBUG (bring-up ISSP "LKBD"): adapter-side view of the keyboard line —
    // is our USB adapter driving it low, and does the shared net follow?
    reg [7:0] kbdsig_edges = 0, kbdwire_edges = 0;
    reg ksig_d = 1, kwire_d = 1;
    always @(posedge clk_sys) begin
        ksig_d  <= kbd_out_sig;
        kwire_d <= kbd_serial_wire;
        if (kbd_out_sig != ksig_d)      kbdsig_edges  <= kbdsig_edges + 1'd1;
        if (kbd_serial_wire != kwire_d) kbdwire_edges <= kbdwire_edges + 1'd1;
    end
    altsource_probe #(
        .sld_auto_instance_index ("YES"), .sld_instance_index (0),
        .instance_id ("LKBD"), .probe_width (32), .source_width (1),
        .source_initial_value ("0"), .enable_metastability ("NO")
    ) u_kbd_probe ( .source(kbd_mute), .probe({kbdsig_edges, kbdwire_edges,
        kbd_out_sig, kbd_serial_wire, 14'd0}), .source_clk(clk_sys), .source_ena(1'b1) );

    // Mouse Adaptor
    wire [6:0] m_lisa_quad;

    // ps2_mouse[24] toggle -> one consumed report per packet (see note below)
    reg mouse_report_pend = 0;
    reg m24_d = 0;
    always @(posedge clk_sys) begin
        m24_d <= ps2_mouse[24];
        if (usbclk_en && mouse_report_pend) mouse_report_pend <= 1'b0;
        if (ps2_mouse[24] != m24_d) mouse_report_pend <= 1'b1;
    end

    // Use usbclk from the clock_divider module (via top's usbclk output)
    // DEBUG (ISSP "LMOU"): confirm mouse events reach the adapter. Counts
    // report pulses actually consumed on usbclk_en ticks, packet toggles, and
    // shows the last dx/dy/buttons. If pkt_cnt climbs when you move the mouse
    // but rep_cnt doesn't, the report pulse is being missed.
    reg [15:0] mou_pkt_cnt = 0, mou_rep_cnt = 0;
    always @(posedge clk_sys) begin
        if (ps2_mouse[24] != m24_d) mou_pkt_cnt <= mou_pkt_cnt + 1'd1;
        if (usbclk_en && mouse_report_pend) mou_rep_cnt <= mou_rep_cnt + 1'd1;
    end
    altsource_probe #(
        .sld_auto_instance_index ("YES"), .sld_instance_index (0),
        .instance_id ("LMOU"), .probe_width (64), .source_width (1),
        .source_initial_value ("0"), .enable_metastability ("NO")
    ) u_mou_probe ( .source(), .probe({ mou_pkt_cnt, mou_rep_cnt,
        ps2_mouse[15:8], ps2_mouse[23:16], ps2_mouse[7:0], mouse_report_pend, 7'd0 }),
        .source_clk(clk_sys), .source_ena(1'b1) );

    usb_mouse_interface mouse_adapter_i (
        .clk_sys(clk_sys),
        .usbclk_en(usbclk_en),
        .usbrst(n_reset),
        // hps_io ps2_mouse layout: [7:0]=flags/buttons (bit0=L,bit1=R,bit2=M),
        // [15:8]=X delta, [23:16]=Y delta (PS/2 Y+ = up), [24]=update TOGGLE
        // (flips each packet — NOT a strobe). Convert the toggle to a one-tick
        // report the adapter consumes on its usbclk_en pacing; feeding the raw
        // toggle made the adapter re-latch the same delta for whole half-periods.
        .mouse_dx_in(ps2_mouse[15:8]),
        .mouse_dy_in(-ps2_mouse[23:16]), // Invert Y delta for Mac/Lisa standard
        .mouse_btn_in({5'b0, ps2_mouse[2], ps2_mouse[1], ps2_mouse[0]}),
        .report(mouse_report_pend),
        .M(m_lisa_quad)
    );

    // Parallel Hard Disk ProFile Emulator
    wire       _CMD_esprofile;
    wire       _BSY_esprofile;
    wire       R_W_esprofile;
    wire       _STRB_esprofile;
    wire       _PRES_esprofile;
    wire       _PARITY_esprofile;
    // Explicit ProFile data bus (was a shared tri-state net): the profile
    // emulator drives when pd_oe, otherwise the Lisa side's value (FF = idle
    // pull-up emulated inside top) is on the bus.
    wire [7:0] pd_top_out;
    wire [7:0] profile_pd_out;
    wire       profile_pd_oe;
    wire [7:0] pd_esprofile = profile_pd_oe ? profile_pd_out : pd_top_out;

    profile profile_i (
        .clk(clk_sys),
        .reset(!n_reset),

        // Core parallel lines
        ._PRES(_PRES_esprofile),
        ._CMD(_CMD_esprofile),
        ._PSTRB(_STRB_esprofile),
        .R_W(R_W_esprofile),
        ._BSY(_BSY_esprofile),
        ._PARITY(_PARITY_esprofile),
        .PD_i(pd_esprofile),
        .PD_o(profile_pd_out),
        .PD_oe_o(profile_pd_oe),

        // HPS sector interface
        .sd_lba(sd_lba[0]),
        .sd_rd(sd_rd),
        .sd_wr(sd_wr),
        .sd_ack(sd_ack),
        .sd_buff_addr(sd_buff_addr),
        .sd_buff_dout(sd_buff_dout),
        .sd_buff_din(sd_buff_din[0]),
        .sd_buff_wr(sd_buff_wr),
        .img_mounted(img_mounted)
    );

    // Auto power-on: the Lisa's COP only powers the machine on when it sees a
    // power-button press (a falling edge on _PWRSW). _PWRSW was tied high, so the
    // machine could never turn on. Hold it high for ~0.4s after config (let the
    // COP finish its internal power-on reset), then drive it low once to simulate
    // the button press. top.sv stretches this into a bounded pulse to the COP.
    reg [25:0] pwron_cnt = 26'd0;
    reg        lisa_pwrsw_n = 1'b1;
    always @(posedge clk_sys) begin
        if (!pwron_cnt[25]) pwron_cnt <= pwron_cnt + 26'd1; // ~2^25/81.5MHz ~= 0.41s
        else                lisa_pwrsw_n <= 1'b0;           // then assert the power press
    end

    // Instantiate Apple Lisa Motherboard core (top)
    top core (
        .sysclk(CLK_50M), // Legacy 50MHz reference (still used by some sys-domain logic inside top)
        .clk_sys(clk_sys), // 81.50016 MHz master; all Lisa clocks divided from this

        // Video
        ._VSYNC(_VSYNC_core),
        ._HSYNC(_HSYNC_core),
        .VID(VID_core),
        .CONT(CONT_core),
        .INVID(1'b1), // REG jumper position on the LisaFPGA board (top inverts it; 0 here = inverted video)
        .SCANLINES(1'b0),
        .FRAMERATE_SEL(1'b1),

        // Audio
        .TONE(TONE_core),
        .VC(VC_core),

        // HDMI (unused, assign outputs to dummy ports)
        .HDMI_CLK_N(),
        .HDMI_CLK_P(),
        .HDMI_D_N(),
        .HDMI_D_P(),

        // SRAM (mapped to SDRAM)
        ._CE_SRAM(_CE_SRAM),
        ._OE_SRAM(_OE_SRAM),
        ._WE_SRAM(_WE_SRAM),
        ._UDS_SRAM(_UDS_SRAM),
        ._LDS_SRAM(_LDS_SRAM),
        .A_SRAM(A_SRAM),
        .D_SRAM(D_SRAM),

        // Floppy (unimplemented/stubs for now)
        .RAM_SEL(status[4:3]),
        .ESFLOPPY_COMM_BUS(),
        .RDA_ESFLOPPY(1'b1),
        .WRD_ESFLOPPY(),
        .SNS_ESFLOPPY(1'b1),
        ._WRQ_ESFLOPPY(),
        .HDS_ESFLOPPY(),
        .PH_ESFLOPPY(),
        .MT1_ESFLOPPY(),
        .MT0_ESFLOPPY(),
        ._DR1_ESFLOPPY(),
        ._DR0_ESFLOPPY(),
        .PWM_ESFLOPPY(),
        .LEFT_ESFLOPPY(1'b1),
        .OK_ESFLOPPY(1'b1),
        .RIGHT_ESFLOPPY(1'b1),

        .RDA_EXTFLOPPY(1'b1),
        .WRD_EXTFLOPPY(),
        .SNS_EXTFLOPPY(1'b1),
        ._WRQ_EXTFLOPPY(),
        .HDS_EXTFLOPPY(),
        .PH_EXTFLOPPY(),
        .MT1_EXTFLOPPY(),
        .MT0_EXTFLOPPY(),
        ._DR1_EXTFLOPPY(),
        ._DR0_EXTFLOPPY(),
        .PWM_EXTFLOPPY(),
        .FLOPPY_SRC(1'b0),

        // ProFile Hard Disk
        .ESPROFILE_COMM_BUS(),
        ._CMD_ESPROFILE(_CMD_esprofile),
        ._BSY_ESPROFILE(_BSY_esprofile),
        .R_W_ESPROFILE(R_W_esprofile),
        ._STRB_ESPROFILE(_STRB_esprofile),
        ._PRES_ESPROFILE(_PRES_esprofile),
        ._PARITY_ESPROFILE(_PARITY_esprofile),
        .OCD_ESPROFILE(1'b1), // Parity check OK
        .PD_ESPROFILE_in(pd_esprofile),
        .PD_ESPROFILE_out(pd_top_out),

        ._CMD_EXTPROFILE(),
        ._BSY_EXTPROFILE(1'b1),
        .R_W_EXTPROFILE(),
        ._STRB_EXTPROFILE(),
        ._PRES_EXTPROFILE(),
        ._PARITY_EXTPROFILE(1'b0),
        .OCD_EXTPROFILE(1'b0),
        .PD_EXTPROFILE(),

        .HDD_SRC(1'b0), // Always boot from internal ESProFile emulator

        // Keyboard & Mouse
        .KBD_DN(),
        .KBD_DP(),
        .KBD_line_in(kbd_serial_wire),
        .KBD_line_out(kbd_line_out_top),
        .KBD_SEL(1'b0), // Use KBD (Lisa keyboard line)

        .MOUSE_DN(),
        .MOUSE_DP(),
        .M_LISA(m_lisa_quad),
        .MOUSE_SEL(1'b0), // Use M_LISA

        // Extra I/O & Switches
        .GPIO(6'b0),
        .SYNCA(1'b0),
        .TXDA(),
        .RTSA(),
        .DTRA(),
        .RXDA(1'b1),
        .CTSA(1'b1),
        .DCDA(1'b1),
        .TRXCA(),
        .RTXCA(1'b1),
        .TXDB(),
        .DTRB(),
        .RTSB(),
        .RXDB(1'b1),
        .CTSB_TRXCB(1'b1),
        .INTERNAL_SCC_EN(),

        ._PWRSW(lisa_pwrsw_n), // Auto power-on pulse (was tied 1'b1 = never on)
        .ON(),
        ._RSTSW(n_reset),
        ._RESET(),
        ._NMISW(1'b1), // NMI switch normally open (high)

        .SPEED_SEL(status[6:5]),
        .CPU_ROM_SEL(status[7]),
        .IO_ROM_SEL(status[8]),
        .usbclk_en(usbclk_en),
        .pll_locked(pll_locked), // DEBUG (bring-up ISSP)
        .pixel_ce(pixel_ce), // DOTCK-rate pixel enable -> CE_PIXEL
        .dbg_va(dbg_va),   // VA_overflow -> vertical blank
        .dbg_clr(dbg_clr)  // _clr_vid_clk -> horizontal active start
    );

endmodule
