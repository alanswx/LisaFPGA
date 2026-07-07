# Lisa Verilator simulator

This directory contains the Verilator/SDL simulator wrapper for the Lisa core.

## Build

Install Verilator, SDL2, and a C++ compiler, then build from this directory:

```sh
make
```

The executable is written to `obj_dir/Vemu`.

## Run

Run from this directory so the ROM memory symlinks resolve:

```sh
./obj_dir/Vemu
```

By default the simulator tries to mount `profile.image` from this directory.
Disk images are local runtime media and are not tracked. Use `--profile` to
select another ProFile image:

```sh
./obj_dir/Vemu --profile /path/to/profile.image
```

For command-line runs without SDL/ImGui, use `--headless`. `--cycles` limits
the run; `--cycles 0` runs until interrupted.

```sh
./obj_dir/Vemu --headless --profile /path/to/profile.image --cycles 5000000
```

## Notes

`../rtl/t420_notri.v` is a generated Verilog translation of the COP421 VHDL
model. Verilator builds use that file because the rest of the FPGA build still
uses `../rtl/t420_notri.vhd`.
