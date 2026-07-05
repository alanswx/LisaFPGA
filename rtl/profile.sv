// profile.sv
// Apple ProFile Parallel Port Hard Disk Drive Emulator for MiSTer FPGA
//
// Translates 532-byte ProFile blocks to/from 512-byte SD card sectors using a 2-sector cache.
// Handles ProFile parallel protocol handshaking with command decoding and status bytes.

`timescale 1 ps / 1 ps

module profile (
    input  wire        clk,             // System clock (81.5 MHz)
    input  wire        reset,           // Core reset

    // ProFile Parallel Interface (from/to Lisa)
    input  wire        _PRES,           // Reset from Lisa (active low)
    input  wire        _CMD,            // Command line from Lisa (active low)
    input  wire        _PSTRB,          // Strobe line from Lisa (active low)
    input  wire        R_W,             // Read/Write select from Lisa (1 = Read, 0 = Write)
    output reg         _BSY,            // Busy line to Lisa (active low)
    output reg         _PARITY,         // Parity bit to Lisa
    inout  wire [7:0]  PD,              // Bidirectional 8-bit data bus

    // MiSTer HPS SD Card Interface
    output reg  [31:0] sd_lba,          // LBA sector address
    output reg         sd_rd,           // Sector read request
    output reg         sd_wr,           // Sector write request
    input  wire        sd_ack,          // Sector transfer acknowledge
    input  wire  [7:0] sd_buff_addr,    // Sector buffer word address (0-255)
    input  wire [15:0] sd_buff_dout,    // Data from HPS to FPGA
    output wire [15:0] sd_buff_din,     // Data from FPGA to HPS
    input  wire        sd_buff_wr       // Buffer write enable from HPS
);

    // Tri-state buffer control for parallel data bus
    reg  [7:0] pd_out;
    reg        pd_oe;
    assign PD = pd_oe ? pd_out : 8'hZZ;
    wire [7:0] pd_in = PD;

    // Odd parity generation: XOR sum of driven data bits
    always_comb begin
        _PARITY = ^pd_out;
    end

    // 2-Sector cache (1024 bytes BRAM)
    reg [7:0] cache_data[1024];
    reg [31:0] cache_sec0_tag, cache_sec1_tag;
    reg        cache_sec0_valid, cache_sec1_valid;
    reg        cache_sec0_dirty, cache_sec1_dirty;

    // SD Card Interface data mapping (FPGA -> HPS)
    reg active_slot; // 0 = sector 0 (low 512 bytes), 1 = sector 1 (high 512 bytes)
    assign sd_buff_din = (active_slot == 1'b0) ? 
        {cache_data[{1'b0, sd_buff_addr, 1'b1}], cache_data[{1'b0, sd_buff_addr, 1'b0}]} :
        {cache_data[{1'b1, sd_buff_addr, 1'b1}], cache_data[{1'b1, sd_buff_addr, 1'b0}]};

    // Buffer writes from HPS to Cache BRAM
    always_ff @(posedge clk) begin
        if (sd_buff_wr) begin
            if (active_slot == 1'b0) begin
                cache_data[{1'b0, sd_buff_addr, 1'b0}] <= sd_buff_dout[7:0];
                cache_data[{1'b0, sd_buff_addr, 1'b1}] <= sd_buff_dout[15:8];
            end else begin
                cache_data[{1'b1, sd_buff_addr, 1'b0}] <= sd_buff_dout[7:0];
                cache_data[{1'b1, sd_buff_addr, 1'b1}] <= sd_buff_dout[15:8];
            end
        end
    end

    // Command decoding and buffering
    reg [7:0] commandBuffer[6];
    reg [2:0] cmd_idx;
    reg [23:0] block_num;
    reg [31:0] byte_addr;
    reg [31:0] sec_A, sec_B;
    reg [8:0]  block_offset;

    // Shift-and-add multiplication: block_num * 532
    always_comb begin
        byte_addr = (block_num << 9) + (block_num << 4) + (block_num << 2);
        sec_A = byte_addr >> 9;
        sec_B = (byte_addr + 531) >> 9;
        block_offset = byte_addr & 9'h1FF;
    end

    // Edges detection for handshaking signals
    reg pstrb_last;
    reg cmd_last;
    always_ff @(posedge clk) begin
        pstrb_last <= _PSTRB;
        cmd_last <= _CMD;
    end
    wire pstrb_falling = (_PSTRB == 0 && pstrb_last == 1);
    wire pstrb_rising  = (_PSTRB == 1 && pstrb_last == 0);
    wire cmd_falling   = (_CMD == 0 && cmd_last == 1);
    wire cmd_rising    = (_CMD == 1 && cmd_last == 0);

    // Main Emulator FSM States
    typedef enum reg [4:0] {
        ST_RESET,
        ST_IDLE,
        ST_HANDSHAKE_0,
        ST_HANDSHAKE_1,
        ST_CMD_RECV_0,
        ST_CMD_RECV_1,
        ST_CMD_DECODE,
        ST_READ_CONFIRM_0,
        ST_READ_CONFIRM_1,
        ST_READ_CACHE_A,
        ST_READ_CACHE_B,
        ST_READ_DATA_0,
        ST_READ_DATA_1,
        ST_READ_DATA_2,
        ST_WRITE_CONFIRM_0,
        ST_WRITE_CONFIRM_1,
        ST_WRITE_CACHE_A,
        ST_WRITE_CACHE_B,
        ST_WRITE_DATA_0,
        ST_WRITE_DATA_1,
        ST_WRITE_FLUSH_A,
        ST_WRITE_FLUSH_B,
        ST_WRITE_DONE_0,
        ST_WRITE_DONE_1,
        ST_HPS_READ,
        ST_HPS_WRITE,
        ST_BAD_CMD
    } state_t;

    state_t state, return_state;

    // Spare table content (48 bytes + FF padding)
    localparam [7:0] spareTable[48] = '{
        8'h50, 8'h52, 8'h4F, 8'h46, 8'h49, 8'h4C, 8'h45, 8'h20, 8'h20, 8'h20, 8'h20, 8'h20, 8'h20, 8'h00, 8'h00, 8'h00,
        8'h03, 8'h98, 8'h00, 8'h26, 8'h00, 8'h02, 8'h14, 8'h20, 8'h00, 8'h00, 8'hFF, 8'hFF, 8'hFF, 8'hFF, 8'hFF, 8'hFF,
        8'h43, 8'h61, 8'h6D, 8'h65, 8'h6F, 8'h2F, 8'h41, 8'h70, 8'h68, 8'h69, 8'h64, 8'h20, 8'h30, 8'h30, 8'h30, 8'h31
    };

    reg [9:0] data_idx; // up to 536 bytes
    reg [7:0] temp_data;
    reg       is_spare_read;

    // Cache hit lookup helper logic
    wire cache_sec0_hit_A = cache_sec0_valid && (cache_sec0_tag == sec_A);
    wire cache_sec1_hit_A = cache_sec1_valid && (cache_sec1_tag == sec_A);
    wire cache_sec0_hit_B = cache_sec0_valid && (cache_sec0_tag == sec_B);
    wire cache_sec1_hit_B = cache_sec1_valid && (cache_sec1_tag == sec_B);

    wire hit_A = cache_sec0_hit_A || cache_sec1_hit_A;
    wire hit_B = cache_sec0_hit_B || cache_sec1_hit_B;

    wire [1'b0:1'b0] slot_A = cache_sec1_hit_A;
    wire [1'b0:1'b0] slot_B = cache_sec1_hit_B;

    // Address translation function for cache BRAM read
    function automatic [9:0] get_cache_addr(input [1:0] dummy, input slot, input [8:0] offset);
        return {slot, offset};
    endfunction

    // Emulator FSM and Cache Logic
    always_ff @(posedge clk) begin
        if (reset || _PRES == 0) begin
            state <= ST_RESET;
            _BSY <= 1'b1;
            pd_oe <= 1'b0;
            sd_rd <= 1'b0;
            sd_wr <= 1'b0;
            cache_sec0_valid <= 0;
            cache_sec1_valid <= 0;
            cache_sec0_dirty <= 0;
            cache_sec1_dirty <= 0;
        end else begin
            case (state)
                ST_RESET: begin
                    _BSY <= 1'b1;
                    pd_oe <= 1'b0;
                    sd_rd <= 1'b0;
                    sd_wr <= 1'b0;
                    state <= ST_IDLE;
                end

                ST_IDLE: begin
                    _BSY <= 1'b1;
                    pd_oe <= 1'b0;
                    if (cmd_falling) begin
                        state <= ST_HANDSHAKE_0;
                    end
                end

                ST_HANDSHAKE_0: begin
                    pd_oe <= 1'b1;
                    pd_out <= 8'h01; // Drive 0x01 on bus
                    _BSY <= 1'b0;    // Acknowledge by pulling BSY low
                    if (_CMD == 1) begin
                        state <= ST_HANDSHAKE_1;
                    end
                end

                ST_HANDSHAKE_1: begin
                    pd_oe <= 1'b0; // Set to input
                    if (pstrb_falling && pd_in == 8'h55) begin
                        state <= ST_CMD_RECV_0;
                    end
                end

                ST_CMD_RECV_0: begin
                    _BSY <= 1'b1; // Raise BSY to show ready
                    cmd_idx <= 0;
                    state <= ST_CMD_RECV_1;
                end

                ST_CMD_RECV_1: begin
                    if (_CMD == 0) begin
                        // Command phase completed when Lisa pulls CMD low
                        state <= ST_CMD_DECODE;
                    end else if (pstrb_falling) begin
                        commandBuffer[cmd_idx] <= pd_in;
                        cmd_idx <= cmd_idx + 1'b1;
                    end
                end

                ST_CMD_DECODE: begin
                    block_num <= {commandBuffer[1], commandBuffer[2], commandBuffer[3]};
                    is_spare_read <= (commandBuffer[1] == 8'hFF && commandBuffer[2] == 8'hFF && commandBuffer[3] == 8'hFF);
                    
                    if (commandBuffer[0] == 8'h00) begin
                        state <= ST_READ_CONFIRM_0;
                    end else if (commandBuffer[0] == 8'h01 || commandBuffer[0] == 8'h02 || commandBuffer[0] == 8'h03) begin
                        state <= ST_WRITE_CONFIRM_0;
                    end else begin
                        state <= ST_BAD_CMD;
                    end
                end

                ST_READ_CONFIRM_0: begin
                    pd_oe <= 1'b1;
                    pd_out <= 8'h02; // Read confirmation
                    _BSY <= 1'b0;
                    if (_CMD == 1) begin
                        state <= ST_READ_CONFIRM_1;
                    end
                end

                ST_READ_CONFIRM_1: begin
                    pd_oe <= 1'b0; // Set to input
                    if (pstrb_falling && pd_in == 8'h55) begin
                        if (is_spare_read) begin
                            state <= ST_READ_DATA_0;
                        end else begin
                            state <= ST_READ_CACHE_A;
                        end
                    end
                end

                // Cache access state machine for Read
                ST_READ_CACHE_A: begin
                    if (hit_A) begin
                        state <= ST_READ_CACHE_B;
                    end else begin
                        // Cache miss on sec_A. Choose replacement slot (LRU or simple flip)
                        active_slot <= 1'b0;
                        sd_lba <= sec_A;
                        if (cache_sec0_valid && cache_sec0_dirty) begin
                            // Must flush dirty sector first
                            state <= ST_HPS_WRITE;
                            return_state <= ST_READ_CACHE_A;
                        end else begin
                            state <= ST_HPS_READ;
                            return_state <= ST_READ_CACHE_A;
                        end
                    end
                end

                ST_READ_CACHE_B: begin
                    if (hit_B) begin
                        state <= ST_READ_DATA_0;
                    end else begin
                        // Cache miss on sec_B. Put it in slot 1
                        active_slot <= 1'b1;
                        sd_lba <= sec_B;
                        if (cache_sec1_valid && cache_sec1_dirty) begin
                            // Must flush dirty sector first
                            state <= ST_HPS_WRITE;
                            return_state <= ST_READ_CACHE_B;
                        end else begin
                            state <= ST_HPS_READ;
                            return_state <= ST_READ_CACHE_B;
                        end
                    end
                end

                ST_READ_DATA_0: begin
                    _BSY <= 1'b1; // Ready for data strobe
                    data_idx <= 0;
                    pd_oe <= 1'b1;
                    state <= ST_READ_DATA_1;
                end

                ST_READ_DATA_1: begin
                    // Status/Data multiplexing
                    if (data_idx < 4) begin
                        pd_out <= 8'h00; // Status bytes 0-3 are zero
                    end else begin
                        if (is_spare_read) begin
                            pd_out <= ((data_idx - 4) < 48) ? spareTable[data_idx - 4] : 8'hFF;
                        end else begin
                            // Read from sector cache BRAM
                            // Sector offset calculation
                            if (block_offset + (data_idx - 4) < 512) begin
                                pd_out <= cache_data[get_cache_addr(2'b00, slot_A, block_offset + (data_idx - 4))];
                            end else begin
                                pd_out <= cache_data[get_cache_addr(2'b00, slot_B, block_offset + (data_idx - 4) - 512)];
                            end
                        end
                    end

                    state <= ST_READ_DATA_2;
                end

                ST_READ_DATA_2: begin
                    if (_CMD == 0) begin
                        // Lisa pulled CMD low to end the transfer
                        state <= ST_IDLE;
                    end else if (pstrb_falling) begin
                        data_idx <= data_idx + 1'b1;
                        state <= ST_READ_DATA_1;
                    end
                end

                ST_WRITE_CONFIRM_0: begin
                    pd_oe <= 1'b1;
                    pd_out <= commandBuffer[0] + 8'h02; // Write response
                    _BSY <= 1'b0;
                    if (_CMD == 1) begin
                        state <= ST_WRITE_CONFIRM_1;
                    end
                end

                ST_WRITE_CONFIRM_1: begin
                    pd_oe <= 1'b0;
                    if (pstrb_falling && pd_in == 8'h55) begin
                        state <= ST_WRITE_CACHE_A;
                    end
                end

                // Cache access state machine for Write (Read-Modify-Write)
                ST_WRITE_CACHE_A: begin
                    if (hit_A) begin
                        state <= ST_WRITE_CACHE_B;
                    end else begin
                        active_slot <= 1'b0;
                        sd_lba <= sec_A;
                        if (cache_sec0_valid && cache_sec0_dirty) begin
                            state <= ST_HPS_WRITE;
                            return_state <= ST_WRITE_CACHE_A;
                        end else begin
                            state <= ST_HPS_READ;
                            return_state <= ST_WRITE_CACHE_A;
                        end
                    end
                end

                ST_WRITE_CACHE_B: begin
                    if (hit_B) begin
                        state <= ST_WRITE_DATA_0;
                    end else begin
                        active_slot <= 1'b1;
                        sd_lba <= sec_B;
                        if (cache_sec1_valid && cache_sec1_dirty) begin
                            state <= ST_HPS_WRITE;
                            return_state <= ST_WRITE_CACHE_B;
                        end else begin
                            state <= ST_HPS_READ;
                            return_state <= ST_WRITE_CACHE_B;
                        end
                    end
                end

                ST_WRITE_DATA_0: begin
                    _BSY <= 1'b1; // Ready for data strobe
                    data_idx <= 0;
                    state <= ST_WRITE_DATA_1;
                end

                ST_WRITE_DATA_1: begin
                    if (_CMD == 0) begin
                        // Host pulled CMD low; finished receiving 532 bytes
                        // Mark cached sectors dirty
                        if (cache_sec0_hit_A) cache_sec0_dirty <= 1'b1;
                        if (cache_sec1_hit_A) cache_sec1_dirty <= 1'b1;
                        if (cache_sec0_hit_B) cache_sec0_dirty <= 1'b1;
                        if (cache_sec1_hit_B) cache_sec1_dirty <= 1'b1;

                        state <= ST_WRITE_FLUSH_A;
                    end else if (pstrb_falling) begin
                        // Save received byte into Cache BRAM
                        if (block_offset + data_idx < 512) begin
                            cache_data[get_cache_addr(2'b00, slot_A, block_offset + data_idx)] <= pd_in;
                        end else begin
                            cache_data[get_cache_addr(2'b00, slot_B, block_offset + data_idx - 512)] <= pd_in;
                        end
                        data_idx <= data_idx + 1'b1;
                    end
                end

                // Write-back flush to SD card
                ST_WRITE_FLUSH_A: begin
                    if (cache_sec0_valid && cache_sec0_dirty && (cache_sec0_tag == sec_A || cache_sec0_tag == sec_B)) begin
                        active_slot <= 1'b0;
                        sd_lba <= cache_sec0_tag;
                        state <= ST_HPS_WRITE;
                        return_state <= ST_WRITE_FLUSH_B;
                    end else begin
                        state <= ST_WRITE_FLUSH_B;
                    end
                end

                ST_WRITE_FLUSH_B: begin
                    if (cache_sec1_valid && cache_sec1_dirty && (cache_sec1_tag == sec_A || cache_sec1_tag == sec_B)) begin
                        active_slot <= 1'b1;
                        sd_lba <= cache_sec1_tag;
                        state <= ST_HPS_WRITE;
                        return_state <= ST_WRITE_DONE_0;
                    end else begin
                        state <= ST_WRITE_DONE_0;
                    end
                end

                ST_WRITE_DONE_0: begin
                    pd_oe <= 1'b1;
                    pd_out <= 8'h06; // Write completed OK
                    _BSY <= 1'b0;
                    if (_CMD == 1) begin
                        state <= ST_WRITE_DONE_1;
                    end
                end

                ST_WRITE_DONE_1: begin
                    pd_oe <= 1'b0;
                    if (pstrb_falling && pd_in == 8'h55) begin
                        state <= ST_IDLE;
                    end
                end

                // HPS SD Card Interface Handshaking states
                ST_HPS_READ: begin
                    sd_rd <= 1'b1;
                    if (sd_ack) begin
                        sd_rd <= 1'b0;
                        if (active_slot == 1'b0) begin
                            cache_sec0_tag <= sd_lba;
                            cache_sec0_valid <= 1'b1;
                            cache_sec0_dirty <= 1'b0;
                        end else begin
                            cache_sec1_tag <= sd_lba;
                            cache_sec1_valid <= 1'b1;
                            cache_sec1_dirty <= 1'b0;
                        end
                        state <= return_state;
                    end
                end

                ST_HPS_WRITE: begin
                    sd_wr <= 1'b1;
                    if (sd_ack) begin
                        sd_wr <= 1'b0;
                        if (active_slot == 1'b0) begin
                            cache_sec0_dirty <= 1'b0;
                        end else begin
                            cache_sec1_dirty <= 1'b0;
                        end
                        state <= return_state;
                    end
                end

                ST_BAD_CMD: begin
                    pd_oe <= 1'b1;
                    pd_out <= 8'h55; // Error status
                    _BSY <= 1'b0;
                    if (_CMD == 1) begin
                        state <= ST_IDLE;
                    end
                end
            endcase
        end
    end

endmodule
