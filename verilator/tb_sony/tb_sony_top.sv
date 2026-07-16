// Thin wrapper that instantiates sony_drive as the Verilator top for the
// standalone drive testbench, and exposes a few internal signals as debug
// outputs (via hierarchical reference) so the C++ testbench can observe the
// GCR byte stream, head position and tachometer without full-system boot.
module tb_sony_top (
    input  wire        clk_sys,
    input  wire        reset,

    input  wire [3:0]  PH,
    input  wire        HDS,
    input  wire        MT0,
    input  wire        MT1,
    input  wire        DR0n,
    input  wire        DR1n,
    input  wire        WRD,
    input  wire        WRQn,
    output wire        rda_serial,

    input  wire        img_mounted,
    input  wire [63:0] img_size,
    output wire        disk_present,

    output wire [31:0] sd_lba,
    output wire        sd_rd,
    output wire        sd_wr,
    input  wire        sd_ack,
    input  wire  [7:0] sd_buff_addr,
    input  wire [15:0] sd_buff_dout,
    output wire [15:0] sd_buff_din,
    input  wire        sd_buff_wr,

    // debug taps
    output wire        dbg_enc_ready,
    output wire [7:0]  dbg_enc_odata,
    output wire [6:0]  dbg_track,
    output wire        dbg_tach,
    output wire [3:0]  dbg_ldstate,
    output wire [3:0]  dbg_encstate,
    output wire [3:0]  dbg_sector,
    output wire [6:0]  dbg_loaded,
    output wire [19:0] dbg_datasrc,
    output wire [9:0]  dbg_srcoff,
    output wire        dbg_strobe,
    output wire [7:0]  dbg_idata,
    output wire [3:0]  dbg_encsec,
    output wire [8:0]  dbg_bitdiv,
    output wire [3:0]  dbg_cellidx,
    output wire        dbg_flux
);

    sony_drive dut (
        .clk_sys(clk_sys),
        .reset(reset),
        .PH(PH),
        .HDS(HDS),
        .MT0(MT0),
        .MT1(MT1),
        ._DR0(DR0n),
        ._DR1(DR1n),
        .WRD(WRD),
        ._WRQ(WRQn),
        .rda_serial(rda_serial),
        .img_mounted(img_mounted),
        .img_size(img_size),
        .disk_present(disk_present),
        .sd_lba(sd_lba),
        .sd_rd(sd_rd),
        .sd_wr(sd_wr),
        .sd_ack(sd_ack),
        .sd_buff_addr(sd_buff_addr),
        .sd_buff_dout(sd_buff_dout),
        .sd_buff_din(sd_buff_din),
        .sd_buff_wr(sd_buff_wr)
    );

    assign dbg_enc_ready = dut.enc_ready;
    assign dbg_enc_odata = dut.enc_odata;
    assign dbg_track     = dut.driveTrack;
    assign dbg_tach      = dut.tach_q;
    assign dbg_ldstate   = {1'b0, dut.ld_state};
    assign dbg_encstate  = dut.enc.state;
    assign dbg_sector    = dut.enc.sector;
    assign dbg_loaded    = dut.loaded_track;
    assign dbg_datasrc   = dut.data_src;
    assign dbg_srcoff    = dut.enc.src_offset;
    assign dbg_strobe    = dut.enc.strobe;
    assign dbg_idata     = dut.enc.data_latch;   // the byte actually fed to the nibbler
    assign dbg_encsec    = dut.enc_sector;
    assign dbg_bitdiv    = dut.bit_div;
    assign dbg_cellidx   = dut.cell_idx;
    assign dbg_flux      = dut.flux;

endmodule
