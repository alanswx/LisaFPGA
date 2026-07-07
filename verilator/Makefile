V = verilator
COSIM = n

TOP = --top-module emu
RTL = ../rtl
V_INC = +incdir+$(RTL)

V_DEFINE = +define+debug=1 +define+SIMULATION=1   -CFLAGS "-I../sim/imgui -I../sim/vinc -I../sim/ -O3 "  --timescale-override 1ps/1ps -Wno-TIMESCALEMOD  \
	-I$(RTL)  \
	-I..
#V_DEFINE += --converge-limit 2000 -Wno-WIDTH -Wno-IMPLICIT -Wno-MODDUP -Wno-UNSIGNED -Wno-CASEINCOMPLETE -Wno-CASEX -Wno-SYMRSVDWORD -Wno-COMBDLY -Wno-INITIALDLY -Wno-BLKANDNBLK -Wno-UNOPTFLAT -Wno-SELRANGE -Wno-CMPCONST -Wno-CASEOVERLAP -Wno-PINMISSING -Wno-MULTIDRIVEN
#V_DEFINE += --threads 8  # this slows it way down
#V_DEFINE += --trace-fst
V_DEFINE +=

UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S), Darwin) #APPLE
	ECHO_MESSAGE = "Mac OS X"
	LIBS += -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo `sdl2-config --libs`
	LIBS += -L/usr/local/lib -L/opt/local/lib

	CXXFLAGS += `sdl2-config --cflags` -Iimgui
	CXXFLAGS += `sdl2-config --cflags | sed 's|include/SDL2|include/|'`
	CXXFLAGS += -I/usr/local/include -I/opt/local/include
	CFLAGS = $(CXXFLAGS) -Iimgui
endif

ifeq ($(UNAME_S), Linux) #LINUX
	ECHO_MESSAGE = "Linux"
	LIBS += -lGL -ldl `sdl2-config --libs`

	CXXFLAGS += `sdl2-config --cflags` -Iimgui
	CFLAGS = $(CXXFLAGS)
endif

ifeq ($(findstring MINGW,$(UNAME_S)),MINGW)
	ECHO_MESSAGE = "MinGW"
	LIBS += -lgdi32 -lopengl32 -limm32 `pkg-config --static --libs sdl2`

	CXXFLAGS += `pkg-config --cflags sdl2`
	CFLAGS = $(CXXFLAGS)
endif

CFLAGS += $(CC_OPT) $(CC_DEFINE) -Iimgui
LDFLAGS = $(LIBS)
EXE = ./obj_dir/Vemu
#V_OPT = -O3 --x-assign fast --x-initial fast --noassert
V_OPT = -O3 --x-assign fast --x-initial 0 --noassert -Wno-fatal
#CC_OPT = -O
CC_OPT = -O3

V_SRC = \
	sim.v  \
	$(RTL)/sdram_ctrl.sv \
	$(RTL)/ps2_kbd.sv \
	$(RTL)/ps2_mouse.v \
	$(RTL)/pll.sv \
	$(RTL)/clock_divider.v \
	$(RTL)/debug_issp.sv \
	$(RTL)/dotck_mmcm.v \
	$(RTL)/profile.sv \
	$(RTL)/ps2_to_usb_hid.sv \
	$(RTL)/usb_keyboard_interface.sv \
	$(RTL)/6502.v \
	$(RTL)/ALU.v \
	$(RTL)/AM9512_FPU.sv \
	$(RTL)/CPU_board.sv \
	$(RTL)/IOROM_2732.sv \
	$(RTL)/IO_RAM_444C_3.sv \
	$(RTL)/IO_board.sv \
	$(RTL)/LS323_shiftreg.sv \
	$(RTL)/Lite_Adapter.sv \
	$(RTL)/MMU_RAM_2148.sv \
	$(RTL)/PROM_6309.sv \
	$(RTL)/RAM_4164.sv \
	$(RTL)/RAM_matrix.sv \
	$(RTL)/SDRAM_Controller_Flat.sv \
	$(RTL)/addressable_latch_LS259.sv \
	$(RTL)/bootrom_2764.sv \
	$(RTL)/decoder_2to4.sv \
	$(RTL)/decoder_3to8.sv \
	$(RTL)/encoder_8to3_LS148.sv \
	$(RTL)/fx68k.sv \
	$(RTL)/fx68kAlu.sv \
	$(RTL)/mem_board_512k.sv \
	$(RTL)/mem_board_2mb.sv \
	$(RTL)/parity_generator_LS280.sv \
	$(RTL)/stubs.sv \
	$(RTL)/usb_mouse_interface.sv \
	$(RTL)/uaddrPla.sv \
	$(RTL)/via6522.v \
	$(RTL)/z8530_scc.sv \
	$(RTL)/t420_notri.v \
	$(RTL)/top.sv





C_SRC = \
	sim_main.cpp  \
	sim/sim_bus.cpp sim/sim_blkdevice.cpp sim/sim_clock.cpp sim/sim_console.cpp sim/sim_video.cpp sim/sim_console.cpp sim/sim_input.cpp  sim/sim_audio.cpp sim/m68kdasm.c sim/m68k_dasm_stubs.cpp \
	sim/imgui/imgui_impl_sdl.cpp sim/imgui/imgui_impl_opengl2.cpp sim/imgui/imgui_draw.cpp sim/imgui/imgui_widgets.cpp sim/imgui/imgui_tables.cpp sim/imgui/imgui.cpp sim/imgui/ImGuiFileDialog.cpp sim/imgui/implot.cpp sim/imgui/implot_items.cpp

VOUT = obj_dir/Vemu.cpp

all: $(EXE)

$(VOUT): $(V_SRC)  Makefile
	$V -cc $(V_OPT) -LDFLAGS "$(LDFLAGS) " -exe  --Mdir ./obj_dir $(V_DEFINE) $(V_INC) $(TOP) -CFLAGS "$(CFLAGS)" $(V_SRC) $(C_SRC)
	#$V -cc $(V_OPT) -LDFLAGS "$(LDFLAGS) " -exe --trace --Mdir ./obj_dir $(V_DEFINE) $(V_INC) $(TOP) -CFLAGS $(CFLAGS) $(V_SRC) $(C_SRC)

$(EXE): $(VOUT) $(C_SRC)
#	(cd obj_dir; make OPT="-fauto-inc-dec -fdce -fdefer-pop -fdse -ftree-ccp -ftree-ch -ftree-fre -ftree-dce -ftree-dse" -f Vemu.mk)
	(cd obj_dir; make -f Vemu.mk)

fast:
	(cd obj_dir; rm -f *.o ; make OPT="-fcompare-elim -fcprop-registers -fguess-branch-probability -fauto-inc-dec -fif-conversion2 -fif-conversion -fipa-pure-const -fdce -fipa-profile -fipa-reference -fmerge-constants -fsplit-wide-types -fdefer-pop -fdse -ftree-ccp -ftree-ch -ftree-fre -ftree-dce -ftree-dse -ftree-builtin-call-dce -ftree-copyrename -ftree-dominator-opts -ftree-forwprop -ftree-phiprop -ftree-sra -ftree-pta -ftree-ter -funit-at-a-time -ftree-bit-ccp -falign-functions  -falign-jumps -falign-loops  -falign-labels -fcaller-saves -fcrossjumping -fcse-follow-jumps -fcse-skip-blocks -fdelete-null-pointer-checks -fdevirtualize -fexpensive-optimizations -fgcse  -fgcse-lm -finline-small-functions -findirect-inlining -fipa-sra -foptimize-sibling-calls -fpartial-inlining -fpeephole2 -fregmove -freorder-blocks  -freorder-functions -frerun-cse-after-loop -fsched-interblock  -fsched-spec -fschedule-insns -fschedule-insns2 -fstrict-aliasing -fstrict-overflow -ftree-switch-conversion -ftree-pre -ftree-vrp" -f Vemu.mk)

clean:
	rm -f obj_dir/*
