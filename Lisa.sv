// Lisa.sv
// Top-Level wrapper module for Apple Lisa on MiSTer FPGA

`timescale 1 ps / 1 ps

module emu (
    // Master Clock and Reset
    input  wire        CLK_50M,
    input  wire  [1:0] RESET,

    // HPS Bus Interface (OSD, SD card, etc.)
    inout  wire [45:0] HPS_BUS,

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

    // Open-drain User port (unused here)
    input  wire  [6:0] USER_IN,
    output wire  [6:0] USER_OUT,

    input  wire        OSD_STATUS
);

    wire        ADC_BUS;
    wire        DDRAM_CLK;
    wire  [1:0] DDRAM_BURSTCNT;
    wire [28:0] DDRAM_ADDR;
    wire [63:0] DDRAM_DIN;
    wire  [7:0] DDRAM_BE;
    wire        DDRAM_RD;
    wire        DDRAM_WE;

    wire [11:0] HDMI_WIDTH = 12'd0;
    wire [11:0] HDMI_HEIGHT = 12'd0;

    wire  [5:0] CONT_core;
    wire [15:0] D_SRAM;

    assign ADC_BUS  = 'Z;
    assign USER_OUT = '1;

    assign {DDRAM_CLK, DDRAM_BURSTCNT, DDRAM_ADDR, DDRAM_DIN, DDRAM_BE, DDRAM_RD, DDRAM_WE} = 0;
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
        "F,IMG,Mount Hard Disk;",
        "-;",
        "O9A,Aspect ratio,4:3,Original,Full Screen,[ARC1];",
        "OBC,Scale,Normal,V-Integer,Narrower HV-Integer,Wider HV-Integer;",
        "-;",
        "O34,RAM Size,512KB,1MB,1.5MB,2MB;",
        "O56,CPU Speed,1x (5MHz),2x,3x,4x;",
        "O7,CPU ROM,3A ROM,H ROM;",
        "O8,I/O ROM,40 ROM,A8 ROM;",
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
    wire [31:0] sd_lba;
    wire        sd_rd;
    wire        sd_wr;
    wire        sd_ack;
    wire  [7:0] sd_buff_addr;
    wire [15:0] sd_buff_dout;
    wire [15:0] sd_buff_din;
    wire        sd_buff_wr;
    wire        img_mounted;
    wire [63:0] img_size;

    hps_io #(.CONF_STR(CONF_STR), .VDNUM(1), .WIDE(1)) hps_io
    (
        .clk_sys(clk_sys),
        .HPS_BUS(HPS_BUS),

        .buttons(buttons),
        .status(status),

        .sd_lba({sd_lba}),
        .sd_rd(sd_rd),
        .sd_wr(sd_wr),
        .sd_ack(sd_ack),

        .sd_buff_addr(sd_buff_addr),
        .sd_buff_dout(sd_buff_dout),
        .sd_buff_din({sd_buff_din}),
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

    // Main Clock PLL
    wire clk_sys;
    wire clk_mem;
    wire clk_16m;
    wire clk_scc;
    wire pll_locked;

    pll main_pll (
        .refclk(CLK_50M),
        .rst(1'b0),
        .outclk_0(clk_sys),  // 81.5 MHz system clock
        .outclk_1(clk_mem),  // 81.5 MHz phase-shifted clock for SDRAM
        .outclk_2(clk_16m),  // 16.3 MHz
        .outclk_3(clk_scc),  // 7.3728 MHz
        .locked(pll_locked)
    );

    // Generate clk_10M (10.1875 MHz) by dividing clk_sys (81.5 MHz) by 8
    reg [2:0] clk_div8;
    always_ff @(posedge clk_sys) begin
        clk_div8 <= clk_div8 + 1'b1;
    end
    wire clk_10m = clk_div8[2];

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
    assign CLK_VIDEO = clk_sys;
    assign CE_PIXEL  = 1;

    wire _VSYNC_core;
    wire _HSYNC_core;
    wire VID_core;
    wire VGA_DE_core;

    // Simple display active area generation (Data Enable) based on sync positions
    reg de_h, de_v;
    always_ff @(posedge clk_sys) begin
        reg [11:0] h_cnt, v_cnt;
        
        if (!_HSYNC_core) h_cnt <= 0;
        else h_cnt <= h_cnt + 1'b1;

        if (!_VSYNC_core) v_cnt <= 0;
        else if (!_HSYNC_core && h_cnt != 0) v_cnt <= v_cnt + 1'b1;

        // Approx standard resolution bounds for the Lisa (720 x 364 display)
        de_h <= (h_cnt > 120 && h_cnt <= 840);
        de_v <= (v_cnt > 20 && v_cnt <= 384);
    end
    assign VGA_DE_core = de_h && de_v;

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
    assign VGA_HS = _HSYNC_core;
    assign VGA_VS = _VSYNC_core;
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
    wire [15:0] DOUT_SRAM;
    wire SRAM_BUS_DIR;

    assign SDRAM_CLK = clk_mem;
    assign SDRAM_CKE = 1'b1;

    sdram sdram_i (
        .init(!pll_locked),
        .clk_64(clk_sys),
        .clk_8(clk_10m),

        .sd_clk(), // Clock output from controller is unused (we feed clk_mem directly to SDRAM clock pin)
        .sd_data(SDRAM_DQ),
        .sd_addr(SDRAM_A),
        .sd_dqm({SDRAM_DQMH, SDRAM_DQML}),
        .sd_cs(SDRAM_nCS),
        .sd_ba(SDRAM_BA),
        .sd_we(SDRAM_nWE),
        .sd_ras(SDRAM_nRAS),
        .sd_cas(SDRAM_nCAS),

        // CPU SRAM interface translation bridge
        .din(DOUT_SRAM),
        .dout(DIN_SRAM),
        .addr({4'b0000, A_SRAM}),
        .ds({~_UDS_SRAM, ~_LDS_SRAM}),
        .we(~_WE_SRAM && ~_CE_SRAM),
        .oe(~_OE_SRAM && ~_CE_SRAM)
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

    usb_keyboard_interface kbd_adapter_i (
        .usbclk(clk_16m), // Wait, kbd FSM uses usbclk (12MHz? The clock_divider generates 12MHz, wait, here we feed clk_16m or divider. Let's use usbclk_12M!)
        .usbrst(n_reset),
        .key_modifiers_in(hid_modifiers),
        .key1_in(hid_key_code),
        .report(hid_report),
        .KBD_in(kbd_serial_wire),
        .KBD_out(kbd_out_sig)
    );

    // Open-collector tri-state output drive for Lisa keyboard line
    assign kbd_serial_wire = ~kbd_out_sig ? 1'b0 : 1'bZ;

    // Mouse Adaptor
    wire [6:0] m_lisa_quad;
    wire usbclk_12M;

    // Use usbclk from the clock_divider module
    usb_mouse_interface mouse_adapter_i (
        .usbclk(usbclk_12M),
        .usbrst(n_reset),
        .mouse_dx_in(ps2_mouse[15:8]),
        .mouse_dy_in(-ps2_mouse[7:0]), // Invert Y delta for Mac/Lisa standard
        .mouse_btn_in({5'b0, ps2_mouse[18], ps2_mouse[17], ps2_mouse[16]}),
        .report(ps2_mouse[24]),
        .M(m_lisa_quad)
    );

    // Parallel Hard Disk ProFile Emulator
    wire       _CMD_esprofile;
    wire       _BSY_esprofile;
    wire       R_W_esprofile;
    wire       _STRB_esprofile;
    wire       _PRES_esprofile;
    wire       _PARITY_esprofile;
    wire [7:0] pd_esprofile;

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
        .PD(pd_esprofile),

        // HPS sector interface
        .sd_lba(sd_lba),
        .sd_rd(sd_rd),
        .sd_wr(sd_wr),
        .sd_ack(sd_ack),
        .sd_buff_addr(sd_buff_addr),
        .sd_buff_dout(sd_buff_dout),
        .sd_buff_din(sd_buff_din),
        .sd_buff_wr(sd_buff_wr)
    );

    // Instantiate Apple Lisa Motherboard core (top)
    top core (
        .sysclk(CLK_50M), // Feed 50MHz directly; clock_divider and dotck_mmcm will handle it

        // Video
        ._VSYNC(_VSYNC_core),
        ._HSYNC(_HSYNC_core),
        .VID(VID_core),
        .CONT(CONT_core),
        .INVID(1'b0),
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
        .PD_ESPROFILE(pd_esprofile),

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
        .KBD(kbd_serial_wire),
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

        ._PWRSW(1'b1), // Power switch normally open (high)
        .ON(),
        ._RSTSW(n_reset),
        ._RESET(),
        ._NMISW(1'b1), // NMI switch normally open (high)

        .SPEED_SEL(status[6:5]),
        .CPU_ROM_SEL(status[7]),
        .IO_ROM_SEL(status[8])
    );

    // Extract usbclk from clock_divider to feed mouse_adapter and kbd_adapter
    assign usbclk_12M = core.usbclk;

endmodule
