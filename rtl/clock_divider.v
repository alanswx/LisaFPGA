// clock_divider.v
// Emulation of Xilinx primary clock divider using Altera PLL and phase accumulators

`timescale 1 ps / 1 ps

module clock_divider (
    input  wire  sysclk,     // 50 MHz input
    output wire  C16M,       // 16.300032 MHz
    output wire  COPCK_2x,   // 3.90 MHz
    output wire  SCCCK_2x,   // 7.3728 MHz
    output wire  C5M,        // 5.09376 MHz
    output wire  usbclk      // 12.00 MHz
);

    wire locked;
    wire c16m_internal;

    altera_pll #(
        .fractional_vco_multiplier("true"),
        .reference_clock_frequency("50.0 MHz"),
        .operation_mode("direct"),
        .number_of_clocks(4),
        .output_clock_frequency0("16.300032 MHz"),
        .phase_shift0("0 ps"),
        .duty_cycle0(50),
        .output_clock_frequency1("7.3728 MHz"),
        .phase_shift1("0 ps"),
        .duty_cycle1(50),
        .output_clock_frequency2("3.90 MHz"),
        .phase_shift2("0 ps"),
        .duty_cycle2(50),
        .output_clock_frequency3("12.00 MHz"),
        .phase_shift3("0 ps"),
        .duty_cycle3(50)
    ) pll_i (
        .refclk(sysclk),
        .rst(1'b0),
        .outclk({usbclk, COPCK_2x, SCCCK_2x, c16m_internal}),
        .locked(locked)
    );

    assign C16M = c16m_internal;

    // Generate C5M (5.09376 MHz) by dividing C16M (16.300032 MHz) by 3.2
    // using a phase accumulator. Increment = (5.09376 / 16.300032) * 2^32 = 1342177280
    reg [31:0] c5m_acc;
    always @(posedge c16m_internal) begin
        c5m_acc <= c5m_acc + 32'd1342177280;
    end
    assign C5M = c5m_acc[31];

endmodule
