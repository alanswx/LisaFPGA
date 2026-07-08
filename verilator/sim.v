`timescale 1ns / 1ps

module emu (
        input clk_sys,
        input reset,
        input soft_reset,
        input menu,
        input adam,

        input [31:0] joystick_0,
        input [31:0] joystick_1,
        input [31:0] joystick_2,
        input [31:0] joystick_3,
        input [31:0] joystick_4,
        input [31:0] joystick_5,

        input [15:0] joystick_l_analog_0,
        input [15:0] joystick_l_analog_1,
        input [15:0] joystick_l_analog_2,
        input [15:0] joystick_l_analog_3,
        input [15:0] joystick_l_analog_4,
        input [15:0] joystick_l_analog_5,

        input [15:0] joystick_r_analog_0,
        input [15:0] joystick_r_analog_1,
        input [15:0] joystick_r_analog_2,
        input [15:0] joystick_r_analog_3,
        input [15:0] joystick_r_analog_4,
        input [15:0] joystick_r_analog_5,

        input [7:0] paddle_0,
        input [7:0] paddle_1,
        input [7:0] paddle_2,
        input [7:0] paddle_3,
        input [7:0] paddle_4,
        input [7:0] paddle_5,

        input [8:0] spinner_0,
        input [8:0] spinner_1,
        input [8:0] spinner_2,
        input [8:0] spinner_3,
        input [8:0] spinner_4,
        input [8:0] spinner_5,

        input [10:0] ps2_key,
        input [24:0] ps2_mouse,
        input [15:0] ps2_mouse_ext,

        input [32:0] timestamp,

        output [7:0] VGA_R,
        output [7:0] VGA_G,
        output [7:0] VGA_B,

        output VGA_HS,
        output VGA_VS,
        output VGA_HB,
        output VGA_VB,

        output CE_PIXEL,

        output reg [15:0] AUDIO_L,
        output reg [15:0] AUDIO_R,

        input ioctl_download,
        input ioctl_wr,
        input [24:0] ioctl_addr,
        input [7:0] ioctl_dout,
        input [7:0] ioctl_index,
        output reg ioctl_wait=1'b0,

        output [31:0]           sd_lba[10],
        output [9:0]            sd_rd,
        output [9:0]            sd_wr,
        input [9:0]             sd_ack,
        input [8:0]             sd_buff_addr,
        input [15:0]            sd_buff_dout,
        output [15:0]           sd_buff_din[10],
        input                   sd_buff_wr,
        input [9:0]             img_mounted,
        input                   img_readonly,
        input [63:0]            img_size,
        output                  ON,
        output                  pwrsw_n_out
);

    // Power & Reset logic
    reg [20:0] pwron_cnt = 21'd0;
    reg        pwrsw_n = 1'b1;
    always @(posedge clk_sys) begin
        if (reset) begin
            pwron_cnt    <= 21'd0;
            pwrsw_n      <= 1'b1;
        end else begin
            if (!pwron_cnt[20]) pwron_cnt <= pwron_cnt + 21'd1;
            else                pwrsw_n   <= 1'b0;
        end
    end
    assign pwrsw_n_out = pwrsw_n;

    // Keyboard Adaptor
    wire [7:0] hid_modifiers;
    wire [7:0] hid_key_code;
    wire       hid_report;

    ps2_to_usb_hid ps2_to_hid_i (
        .clk(clk_sys),
        .reset(reset),
        .ps2_key(ps2_key),
        .key_modifiers(hid_modifiers),
        .key_code(hid_key_code),
        .report(hid_report)
    );

    wire kbd_serial_wire;
    wire kbd_out_sig;
    wire usbclk_en;
    reg kbd_report_pend = 0;
    always @(posedge clk_sys) begin
        if (usbclk_en && kbd_report_pend) kbd_report_pend <= 1'b0;
        if (hid_report) kbd_report_pend <= 1'b1;
    end

    usb_keyboard_interface kbd_adapter_i (
        .clk_sys(clk_sys),
        .usbclk_en(usbclk_en),
        .usbrst(~reset),
        .key_modifiers_in(hid_modifiers),
        .key1_in(hid_key_code),
        .report(kbd_report_pend),
        .KBD_in(kbd_serial_wire),
        .KBD_out(kbd_out_sig)
    );

    wire kbd_line_out_top;
    assign kbd_serial_wire = kbd_line_out_top & kbd_out_sig;

    // Mouse Adaptor
    wire [6:0] m_lisa_quad;
    reg mouse_report_pend = 0;
    reg m24_d = 0;
    always @(posedge clk_sys) begin
        m24_d <= ps2_mouse[24];
        if (ps2_mouse[24] != m24_d) mouse_report_pend <= 1'b1;
        else if (usbclk_en)         mouse_report_pend <= 1'b0;
    end

    usb_mouse_interface mouse_adapter_i (
        .clk_sys(clk_sys),
        .usbclk_en(usbclk_en),
        .usbrst(~reset),
        .mouse_dx_in(ps2_mouse[15:8]),
        .mouse_dy_in(-ps2_mouse[23:16]),
        .mouse_btn_in({5'b0, ps2_mouse[2], ps2_mouse[1], ps2_mouse[0]}),
        .report(mouse_report_pend),
        .M(m_lisa_quad)
    );

    // ProFile Hard Disk Emulator
    wire       _CMD_esprofile;
    wire       _BSY_esprofile;
    wire       R_W_esprofile;
    wire       _STRB_esprofile;
    wire       _PRES_esprofile;
    wire       _PARITY_esprofile;
    wire [7:0] pd_top_out;
    wire [7:0] profile_pd_out;
    wire       profile_pd_oe;
    wire [7:0] pd_to_lisa = profile_pd_oe ? profile_pd_out : pd_top_out;
    wire [7:0] pd_to_profile = !R_W_esprofile ? pd_top_out : pd_to_lisa;

    wire profile_sd_rd;
    wire profile_sd_wr;
    assign sd_rd = { 9'b0, profile_sd_rd };
    assign sd_wr = { 9'b0, profile_sd_wr };
    wire profile_sd_ack = sd_ack[0];
    wire profile_img_mounted = img_mounted[0];

    profile profile_i (
        .clk(clk_sys),
        .reset(reset),

        // Core parallel lines
        ._PRES(_PRES_esprofile),
        ._CMD(_CMD_esprofile),
        ._PSTRB(_STRB_esprofile),
        .R_W(R_W_esprofile),
        ._BSY(_BSY_esprofile),
        ._PARITY(_PARITY_esprofile),
        .PD_i(pd_to_profile),
        .PD_o(profile_pd_out),
        .PD_oe_o(profile_pd_oe),

        // SD controller backend
        .sd_lba(sd_lba[0]),
        .sd_rd(profile_sd_rd),
        .sd_wr(profile_sd_wr),
        .sd_ack(profile_sd_ack),
        .sd_buff_addr(sd_buff_addr[7:0]),
        .sd_buff_dout(sd_buff_dout),
        .sd_buff_din(sd_buff_din[0]),
        .sd_buff_wr(sd_buff_wr),
        .img_mounted(profile_img_mounted)
    );

    // Initialize unused sd_lba and sd_buff_din
    generate
        genvar j;
        for (j = 1; j < 10; j = j + 1) begin : unused_sd_gen
            assign sd_lba[j] = 32'b0;
            assign sd_buff_din[j] = 16'b0;
        end
    endgenerate

    // Lisa Motherboard core top
    wire _VSYNC_core;
    wire _HSYNC_core;
    wire VID_core;
    wire [5:0] CONT_core;
    wire TONE_core;
    wire [2:0] VC_core;
    wire pixel_ce;
    wire dbg_va;
    wire dbg_clr;

    top core (
        .sysclk(clk_sys),
        .clk_sys(clk_sys),

        // Video
        ._VSYNC(_VSYNC_core),
        ._HSYNC(_HSYNC_core),
        .VID(VID_core),
        .CONT(CONT_core),
        .INVID(1'b1),
        .SCANLINES(1'b0),
        .FRAMERATE_SEL(1'b1),

        // Audio
        .TONE(TONE_core),
        .VC(VC_core),

        // HDMI
        .HDMI_CLK_N(),
        .HDMI_CLK_P(),
        .HDMI_D_N(),
        .HDMI_D_P(),

        // SRAM
        ._CE_SRAM(),
        ._OE_SRAM(),
        ._WE_SRAM(),
        ._UDS_SRAM(),
        ._LDS_SRAM(),
        .A_SRAM(),
        .D_SRAM(),

        // Floppy (unimplemented stubs)
        .RAM_SEL(2'b00), // 512KB, matching top.sv's SIMULATION block-RAM board
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
        .OCD_ESPROFILE(1'b0), // OCD is active low: internal ESProFile is present
        .PD_ESPROFILE_in(pd_to_lisa),
        .PD_ESPROFILE_out(pd_top_out),

        ._CMD_EXTPROFILE(),
        ._BSY_EXTPROFILE(1'b1),
        .R_W_EXTPROFILE(),
        ._STRB_EXTPROFILE(),
        ._PRES_EXTPROFILE(),
        ._PARITY_EXTPROFILE(1'b0),
        .OCD_EXTPROFILE(1'b0),
        .PD_EXTPROFILE(),

        .HDD_SRC(1'b0),

        // Keyboard & Mouse
        .KBD_DN(),
        .KBD_DP(),
        .KBD_line_in(kbd_serial_wire),
        .KBD_line_out(kbd_line_out_top),
        .KBD_SEL(1'b0),

        .MOUSE_DN(),
        .MOUSE_DP(),
        .M_LISA(m_lisa_quad),
        .MOUSE_SEL(1'b0),

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

        ._PWRSW(pwrsw_n),
        .ON(ON),
        ._RSTSW(~reset),
        ._RESET(),
        ._NMISW(1'b1),

        .SPEED_SEL(2'b00),
        .CPU_ROM_SEL(1'b0),
        .IO_ROM_SEL(1'b0),
        .usbclk_en(usbclk_en),
        .pll_locked(1'b1),
        .pixel_ce(pixel_ce),
        .dbg_va(dbg_va),
        .dbg_clr(dbg_clr)
    );

    // Audio Output Generation
    reg [14:0] ampl;
    always @(posedge clk_sys) begin
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
        AUDIO_L <= TONE_core ? {1'b0, ampl} : {1'b1, ~ampl + 1'b1};
        AUDIO_R <= TONE_core ? {1'b0, ampl} : {1'b1, ~ampl + 1'b1};
    end

    // Video timing reconstruction
    reg hs_de = 1;
    reg va_line = 1;
    reg va_fell = 0;
    reg [9:0] vcnt = 10'd511;
    reg [10:0] since_hs = 11'd2047;
    reg [10:0] hcnt = 0;
    reg active_h = 0;
    reg v_active = 0;
    reg hs_out = 1;

    localparam [10:0] H_ACTIVE = 11'd720;
    localparam [10:0] HS_WIDTH = 11'd64;
    localparam [9:0] V_ACTIVE = 10'd363;
    wire [10:0] h_de_start = 11'd20;
    wire [10:0] origin_delay = 11'd144;

    always_ff @(posedge clk_sys) if (pixel_ce) begin
        hs_de <= _HSYNC_core;
        if (!hs_de && _HSYNC_core) begin
            since_hs <= 11'd0;
            if (!dbg_va && va_line)
                va_fell <= 1'b1;
            else if (va_fell) begin
                va_fell <= 1'b0; vcnt <= 10'd0;
            end else if (vcnt < 10'd511)
                vcnt <= vcnt + 10'd1;
            va_line <= dbg_va;
        end else if (since_hs != 11'd2047) begin
            since_hs <= since_hs + 11'd1;
        end

        if (since_hs == origin_delay) hcnt <= 11'd0;
        else                          hcnt <= hcnt + 11'd1;

        v_active <= (vcnt < V_ACTIVE);
        active_h <= (hcnt >= h_de_start) && (hcnt < h_de_start + H_ACTIVE);
        hs_out   <= (hcnt < HS_WIDTH);
    end

    assign CE_PIXEL = pixel_ce;
    assign VGA_HS = hs_out;
    assign VGA_VS = _VSYNC_core;
    assign VGA_HB = ~active_h;
    assign VGA_VB = ~v_active;

    // Lisa is monochrome, paint paper white
    assign VGA_R = VID_core ? 8'hFF : 8'h00;
    assign VGA_G = VID_core ? 8'hFF : 8'h00;
    assign VGA_B = VID_core ? 8'hFF : 8'h00;

endmodule
