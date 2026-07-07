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
Disk images are local runtime media and are not tracked. To use another ProFile
image, either pass it as the first argument or set `PROFILE_IMAGE`:

```sh
./obj_dir/Vemu /path/to/profile.image
PROFILE_IMAGE=/path/to/profile.image ./obj_dir/Vemu
```

## Notes

`../rtl/t420_notri.v` is a generated Verilog translation of the COP421 VHDL
model. Verilator builds use that file because the rest of the FPGA build still
uses `../rtl/t420_notri.vhd`.
