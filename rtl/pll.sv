// pll.sv
// Parameterized Altera PLL instance for Apple Lisa core on MiSTer

`timescale 1 ps / 1 ps

module pll (
    input  wire  refclk,   // 50 MHz reference input
    input  wire  rst,      // Active-high reset
    output wire  outclk_0, // 81.50016 MHz (System/Core clock)
    output wire  outclk_1, // 81.50016 MHz (SDRAM clock, phase shifted)
    output wire  outclk_2, // 16.300032 MHz (C16M reference clock)
    output wire  outclk_3, // 7.3728 MHz (SCCCK_2x reference clock)
    output wire  locked    // PLL locked signal
);

    altera_pll #(
        .fractional_vco_multiplier("true"),
        .reference_clock_frequency("50.0 MHz"),
        .operation_mode("direct"),
        .number_of_clocks(4),
        .output_clock_frequency0("81.50016 MHz"),
        .phase_shift0("0 ps"),
        .duty_cycle0(50),
        .output_clock_frequency1("81.50016 MHz"),
        .phase_shift1("-2500 ps"), // -2.5ns phase shift for SDRAM clock
        .duty_cycle1(50),
        .output_clock_frequency2("16.300032 MHz"),
        .phase_shift2("0 ps"),
        .duty_cycle2(50),
        .output_clock_frequency3("7.3728 MHz"),
        .phase_shift3("0 ps"),
        .duty_cycle3(50)
    ) altera_pll_i (
        .refclk(refclk),
        .rst(rst),
        .outclk({outclk_3, outclk_2, outclk_1, outclk_0}),
        .locked(locked)
    );

endmodule
