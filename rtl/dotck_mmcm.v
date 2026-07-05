// dotck_mmcm.v
// Emulation of Xilinx dotck_mmcm using Altera PLL

`timescale 1 ps / 1 ps

module dotck_mmcm (
    input  wire  sysclk,     // 50 MHz input
    output wire  dotck_20M,  // 20.37504 MHz
    output wire  dotck_40M,  // 40.75008 MHz
    output wire  dotck_60M,  // 61.12512 MHz
    output wire  dotck_80M   // 81.50016 MHz
);

    wire locked;

    altera_pll #(
        .fractional_vco_multiplier("true"),
        .reference_clock_frequency("50.0 MHz"),
        .operation_mode("direct"),
        .number_of_clocks(4),
        .output_clock_frequency0("20.37504 MHz"),
        .phase_shift0("0 ps"),
        .duty_cycle0(50),
        .output_clock_frequency1("40.75008 MHz"),
        .phase_shift1("0 ps"),
        .duty_cycle1(50),
        .output_clock_frequency2("61.12512 MHz"),
        .phase_shift2("0 ps"),
        .duty_cycle2(50),
        .output_clock_frequency3("81.50016 MHz"),
        .phase_shift3("0 ps"),
        .duty_cycle3(50)
    ) pll_i (
        .refclk(sysclk),
        .rst(1'b0),
        .outclk({dotck_80M, dotck_60M, dotck_40M, dotck_20M}),
        .locked(locked)
    );

endmodule
