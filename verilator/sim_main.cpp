#include <verilated.h>
#include "Vemu.h"
#include "Vemu__Syms.h"

#include "imgui.h"
#include "implot.h"
#ifndef _MSC_VER
#include <stdio.h>
#include <SDL.h>
#include <SDL_opengl.h>
#else
#define WIN32
#include <dinput.h>
#endif


#define VERILATOR_MAJOR_VERSION (VERILATOR_VERSION_INTEGER / 1000000)

#if VERILATOR_MAJOR_VERSION >= 5
#define VERTOPINTERN top->rootp
#else
#define VERTOPINTERN top
#endif

#include "sim_console.h"
#include "sim_bus.h"
#include "sim_blkdevice.h"
#include "sim_video.h"
#include "sim_audio.h"
#include "sim_input.h"
#include "sim_clock.h"
#include "sim/m68k_dasm.h"
#include "Vemu___024root.h"

bool cpu_trace_enable = false;
int trace_console_cnt = 0;

#define FMT_HEADER_ONLY
#include <fmt/core.h>

#include "../imgui/imgui_memory_editor.h"
#include "../imgui/ImGuiFileDialog.h"

#include <iostream>
#include <sstream>
#include <fstream>
#include <iterator>
#include <string>
#include <iomanip>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <cstdint>

enum class RunState {Stopped, Running, SingleClock, MultiClock, StepIn, NextIRQ};

struct SimOptions {
	bool headless = false;
	uint64_t cycles = 0;
	uint64_t status_interval = 5000000;
	uint64_t status_start = 0;
	uint64_t stop_start = 0;
	uint32_t stop_pc = 0xffffffff;
	bool trace = false;
	bool boot_profile = false;
	bool skip_ram_test = false;
	bool crash_trace = false;
	bool dump_rom_state = false;
	std::string profile_image = "profile.image";
	std::string floppy_image;                 // empty => no floppy mounted
	std::string screenshot;
};

static void PrintUsage(const char* argv0) {
	fprintf(stderr,
		"Usage: %s [--profile <image>] [--headless] [--cycles <count>] [--screenshot <path>] [--boot-profile] [--skip-ram-test] [--crash-trace] [--trace] [--help]\n"
		"\n"
		"  --profile <image>  ProFile disk image to mount (default: profile.image)\n"
		"                     Aliases: --profile-image, --proimage\n"
		"  --headless         Run without SDL/ImGui windows\n"
		"  --cycles <count>   Headless cycles to run; 0 runs until interrupted (default: 0)\n"
		"  --screenshot <path>\n"
		"                     Headless: save the final VGA frame as a binary PPM image\n"
		"  --boot-profile     Headless: select ProFile at the Lisa STARTUP FROM menu\n"
		"  --skip-ram-test    Simulator: skip the ROM's full RAM sweep after sizing\n"
		"  --crash-trace      Headless: dump recent instructions on post-loader HALT/reset\n"
		"  --trace            Enable 68k instruction trace output\n"
		"  --dump-rom-state   Headless: print Lisa ROM scratch/error RAM at exit\n"
		"  --status-interval <count>\n"
		"                     Headless status interval in cycles; 0 disables (default: 5000000)\n"
		"  --status-start <count>\n"
		"                     Headless: suppress periodic status before this cycle count\n"
		"  --stop-pc <addr>   Headless: stop when CPU PC equals this address\n"
		"  --stop-start <count>\n"
		"                     Headless: ignore --stop-pc before this cycle count\n",
		argv0);
}

static bool ParseOptions(int argc, char** argv, SimOptions* options) {
	for (int i = 1; i < argc; i++) {
		std::string arg = argv[i];
		if (arg == "--help" || arg == "-h") {
			PrintUsage(argv[0]);
			exit(0);
		} else if (arg == "--headless") {
			options->headless = true;
		} else if (arg == "--boot-profile") {
			options->boot_profile = true;
		} else if (arg == "--skip-ram-test") {
			options->skip_ram_test = true;
		} else if (arg == "--crash-trace") {
			options->crash_trace = true;
		} else if (arg == "--trace") {
			options->trace = true;
		} else if (arg == "--dump-rom-state") {
			options->dump_rom_state = true;
		} else if (arg == "--profile" || arg == "--profile-image" || arg == "--proimage") {
			if (++i >= argc) {
				fprintf(stderr, "%s requires an image path\n", arg.c_str());
				return false;
			}
			options->profile_image = argv[i];
		} else if (arg.rfind("--profile=", 0) == 0) {
			options->profile_image = arg.substr(10);
		} else if (arg.rfind("--profile-image=", 0) == 0) {
			options->profile_image = arg.substr(16);
		} else if (arg.rfind("--proimage=", 0) == 0) {
			options->profile_image = arg.substr(11);
		} else if (arg == "--floppy" || arg == "--floppy-image") {
			if (++i >= argc) {
				fprintf(stderr, "%s requires an image path\n", arg.c_str());
				return false;
			}
			options->floppy_image = argv[i];
		} else if (arg.rfind("--floppy=", 0) == 0) {
			options->floppy_image = arg.substr(9);
		} else if (arg == "--cycles") {
			if (++i >= argc) {
				fprintf(stderr, "--cycles requires a count\n");
				return false;
			}
			options->cycles = strtoull(argv[i], NULL, 0);
		} else if (arg.rfind("--cycles=", 0) == 0) {
			options->cycles = strtoull(arg.substr(9).c_str(), NULL, 0);
		} else if (arg == "--screenshot") {
			if (++i >= argc) {
				fprintf(stderr, "--screenshot requires a path\n");
				return false;
			}
			options->screenshot = argv[i];
		} else if (arg.rfind("--screenshot=", 0) == 0) {
			options->screenshot = arg.substr(13);
		} else if (arg == "--status-interval") {
			if (++i >= argc) {
				fprintf(stderr, "--status-interval requires a count\n");
				return false;
			}
			options->status_interval = strtoull(argv[i], NULL, 0);
		} else if (arg.rfind("--status-interval=", 0) == 0) {
			options->status_interval = strtoull(arg.substr(18).c_str(), NULL, 0);
		} else if (arg == "--status-start") {
			if (++i >= argc) {
				fprintf(stderr, "--status-start requires a count\n");
				return false;
			}
			options->status_start = strtoull(argv[i], NULL, 0);
		} else if (arg.rfind("--status-start=", 0) == 0) {
			options->status_start = strtoull(arg.substr(15).c_str(), NULL, 0);
		} else if (arg == "--stop-pc") {
			if (++i >= argc) {
				fprintf(stderr, "--stop-pc requires an address\n");
				return false;
			}
			options->stop_pc = strtoul(argv[i], NULL, 0);
		} else if (arg.rfind("--stop-pc=", 0) == 0) {
			options->stop_pc = strtoul(arg.substr(10).c_str(), NULL, 0);
		} else if (arg == "--stop-start") {
			if (++i >= argc) {
				fprintf(stderr, "--stop-start requires a count\n");
				return false;
			}
			options->stop_start = strtoull(argv[i], NULL, 0);
		} else if (arg.rfind("--stop-start=", 0) == 0) {
			options->stop_start = strtoull(arg.substr(13).c_str(), NULL, 0);
		} else {
			fprintf(stderr, "Unknown option: %s\n", arg.c_str());
			PrintUsage(argv[0]);
			return false;
		}
	}
	return true;
}

// Simulation control
// ------------------
int initialReset = 48;
RunState run_state = RunState::Running;
bool adam_mode = 1;
int batchSize = 100000;
int multi_step_amount = 1024;

// Debug GUI 
// ---------
const char* windowTitle = "Verilator Sim: IIgs";
const char* windowTitle_Control = "Simulation control";
const char* windowTitle_DebugLog = "Debug log";
const char* windowTitle_Video = "VGA output";
const char* windowTitle_Audio = "Audio output";
bool showDebugLog = true;
DebugConsole console;
MemoryEditor mem_edit;
char pc_breakpoint[10] = "";
int pc_breakpoint_addr = 0;
bool pc_break_enabled;
bool break_pending = false;
bool old_vpb = false;
bool headless_mode = false;
uint64_t headless_status_interval = 5000000;
uint64_t headless_status_start = 0;
bool headless_boot_profile = false;
bool skip_ram_test = false;
bool ram_test_patch_applied = false;
bool crash_trace = false;
bool headless_boot_profile_started = false;
bool headless_startup_menu_request_started = false;
size_t headless_startup_menu_request_step = 0;
uint64_t headless_boot_profile_ready_time = 0;
size_t headless_boot_profile_step = 0;
std::string headless_screenshot_path;
bool headless_dump_rom_state = false;
uint32_t headless_stop_pc = 0xffffffff;
uint64_t headless_stop_start = 0;
bool headless_stop_requested = false;

// HPS emulator
// ------------
SimBus bus(console);
SimBlockDevice blockdevice(console);

// Input handling
// --------------
SimInput input(13, console);
const int input_right = 0;
const int input_left = 1;
const int input_down = 2;
const int input_up = 3;
const int input_a = 4;
const int input_b = 5;
const int input_x = 6;
const int input_y = 7;
const int input_l = 8;
const int input_r = 9;
const int input_select = 10;
const int input_start = 11;
const int input_menu = 12;

// Video
// -----
#define VGA_WIDTH 720
#define VGA_HEIGHT 364
#define VGA_ROTATE 0  // 90 degrees anti-clockwise
#define VGA_SCALE_X vga_scale
#define VGA_SCALE_Y vga_scale
SimVideo video(VGA_WIDTH, VGA_HEIGHT, VGA_ROTATE);
float vga_scale = 1.0;

// Verilog module
// --------------
Vemu* top = NULL;

vluint64_t main_time = 0;	// Current simulation time.
double sc_time_stamp() {	// Called by $time in Verilog.
	return main_time;
}

int clk_sys_freq = 24000000;
SimClock clk_sys(1);

int soft_reset = 0;
vluint64_t soft_reset_time = 0;

//
// IWM emulation (vestigial IIgs leftovers). defc.h pulls in iwm.h/protos.h from
// the GSplus/KEGS tree, which aren't part of this repo; the only symbol needed
// here is word32. Provide it directly instead of the heavy (unbuildable) include.
typedef uint32_t word32;
int g_c031_disk35;
word32 g_vbl_count;

// Audio
// -----
//#define DISABLE_AUDIO
#ifndef DISABLE_AUDIO
SimAudio audio(clk_sys_freq, false);
#endif

// Reset simulation variables and clocks
void resetSim() {
	main_time = 0;
	top->reset = 1;
	break_pending = false;
	old_vpb = true;
	trace_console_cnt = 0;
	printf("resetSim!! main_time %d top->reset %d\n",main_time,top->reset);
	clk_sys.Reset();
}

//#define DEBUG

bool stop_on_log_mismatch = 1;
bool debug_6502 = 1;
int cpu_sync;
long cpu_instruction_count;
int cpu_clock;
int cpu_clock_last;
const int ins_size = 48;
int ins_index = 0;
unsigned short ins_pc[ins_size];
unsigned char ins_in[ins_size];
unsigned long ins_ma[ins_size];
unsigned char ins_dbr[ins_size];
bool ins_formatted[ins_size];
std::string ins_str[ins_size];

// MAME debug log
const char* tracefilename = "traces/appleiigs.tr";
std::vector<std::string> log_mame;
std::vector<std::string> log_cpu;
long log_index;

bool writeLog(const char* line)
{
	if (debug_6502) {
		// Write to cpu log
		log_cpu.push_back(line);

		// Compare with MAME log
		bool match = true;

		std::string c_line = std::string(line);
		std::string c = "%6d  CPU > " + c_line;
		//printf("%s (%x)\n",line,ins_in[0]); // this has the instruction number
		printf("%s\n",line);

		if (log_index < log_mame.size()) {
			std::string m_line = log_mame.at(log_index);
			std::string m = "%6d MAME > " + m_line;
			if (stop_on_log_mismatch && m_line != c_line) {
				console.AddLog("DIFF at %06d - %06x", cpu_instruction_count, ins_pc[0]);
				console.AddLog(m.c_str(), cpu_instruction_count);
				console.AddLog(c.c_str(), cpu_instruction_count);
				match = false;
			}
			else {
				console.AddLog(c.c_str(), cpu_instruction_count);
			}
		}
		else {
			console.AddLog(c.c_str(), cpu_instruction_count);
		}

		log_index++;
		return match;
	}
	return true;
}

enum instruction_type {
	formatted,
	implied,
	immediate,
	absolute,
	absoluteX,
	absoluteY,
	zeroPage,
	zeroPageX,
	zeroPageY,
	relative,
	relativeLong,
	accumulator,
	direct24,
	direct24X,
	direct24Y,
	indirect,
	indirectX,
	indirectY,
	longValue,
	longX,
	longY,
	stackmode,
	srcdst
};

enum operand_type {
	none,
	byte2,
	byte3
};

struct dasm_data
{
	unsigned short addr;
	const char* name;
};

struct dasm_data32
{
	unsigned long addr;
	const char* name;
};

int a2_name_count;
static const struct dasm_data a2_stuff[] =
{
	{ 0x0020, "WNDLFT" }, { 0x0021, "WNDWDTH" }, { 0x0022, "WNDTOP" }, { 0x0023, "WNDBTM" },
	{ 0x0024, "CH" }, { 0x0025, "CV" }, { 0x0026, "GBASL" }, { 0x0027, "GBASH" },
	{ 0x0028, "BASL" }, { 0x0029, "BASH" }, { 0x002b, "BOOTSLOT" }, { 0x002c, "H2" },
	{ 0x002d, "V2" }, { 0x002e, "MASK" }, { 0x0030, "COLOR" }, { 0x0031, "MODE" },
	{ 0x0032, "INVFLG" }, { 0x0033, "PROMPT" }, { 0x0036, "CSWL" }, { 0x0037, "CSWH" },
	{ 0x0038, "KSWL" }, { 0x0039, "KSWH" }, { 0x0045, "ACC" }, { 0x0046, "XREG" },
	{ 0x0047, "YREG" }, { 0x0048, "STATUS" }, { 0x004E, "RNDL" }, { 0x004F, "RNDH" },
	{ 0x0067, "TXTTAB" }, { 0x0069, "VARTAB" }, { 0x006b, "ARYTAB" }, { 0x6d, "STREND" },
	{ 0x006f, "FRETOP" }, { 0x0071, "FRESPC" }, { 0x0073, "MEMSIZ" }, { 0x0075, "CURLIN" },
	{ 0x0077, "OLDLIN" }, { 0x0079, "OLDTEXT" }, { 0x007b, "DATLIN" }, { 0x007d, "DATPTR" },
	{ 0x007f, "INPTR" }, { 0x0081, "VARNAM" }, { 0x0083, "VARPNT" }, { 0x0085, "FORPNT" },
	{ 0x009A, "EXPON" }, { 0x009C, "EXPSGN" }, { 0x009d, "FAC" }, { 0x00A2, "FAC.SIGN" },
	{ 0x00a5, "ARG" }, { 0x00AA, "ARG.SIGN" }, { 0x00af, "PRGEND" }, { 0x00B8, "TXTPTR" },
	{ 0x00C9, "RNDSEED" }, { 0x00D6, "LOCK" }, { 0x00D8, "ERRFLG" }, { 0x00DA, "ERRLIN" },
	{ 0x00DE, "ERRNUM" }, { 0x00E4, "HGR.COLOR" }, { 0x00E6, "HGR.PAGE" }, { 0x00F1, "SPEEDZ" },

	{ 0xc000, "KBD / 80STOREOFF" }, { 0xc001, "80STOREON" }, { 0xc002, "RDMAINRAM" }, {0xc003, "RDCARDRAM" }, {0xc004, "WRMAINRAM" },
	{ 0xc005, "WRCARDRAM" }, { 0xc006, "SETSLOTCXROM" }, { 0xc007, "SETINTCXROM" }, { 0xc008, "SETSTDZP" },
	{ 0xc009, "SETALTZP "}, { 0xc00a, "SETINTC3ROM" }, { 0xc00b, "SETSLOTC3ROM" }, { 0xc00c, "CLR80VID" },
	{ 0xc00d, "SET80VID" }, { 0xc00e, "CLRALTCHAR" }, { 0xc00f, "SETALTCHAR" }, { 0xc010, "KBDSTRB" },
	{ 0xc011, "RDLCBNK2" }, { 0xc012, "RDLCRAM" }, { 0xc013, "RDRAMRD" }, { 0xc014, "RDRAMWRT" },
	{ 0xc015, "RDCXROM" }, { 0xc016, "RDALTZP" }, { 0xc017, "RDC3ROM" }, { 0xc018, "RD80STORE" },
	{ 0xc019, "RDVBL" }, { 0xc01a, "RDTEXT" }, { 0xc01b, "RDMIXED" }, { 0xc01c, "RDPAGE2" },
	{ 0xc01d, "RDHIRES" }, { 0xc01e, "RDALTCHAR" }, { 0xc01f, "RD80VID" }, { 0xc020, "TAPEOUT" },
	{ 0xc021, "MONOCOLOR" }, { 0xc022, "TBCOLOR" }, { 0xc023, "VGCINT" }, { 0xc024, "MOUSEDATA" },
	{ 0xc025, "KEYMODREG" }, { 0xc026, "DATAREG" }, { 0xc027, "KMSTATUS" }, { 0xc028, "ROMBANK" },
	{ 0xc029, "NEWVIDEO"}, { 0xc02b, "LANGSEL" }, { 0xc02c, "CHARROM" }, { 0xc02d, "SLOTROMSEL" },
	{ 0xc02e, "VERTCNT" }, { 0xc02f, "HORIZCNT" }, { 0xc030, "SPKR" }, { 0xc031, "DISKREG" },
	{ 0xc032, "SCANINT" }, { 0xc033, "CLOCKDATA" }, { 0xc034, "CLOCKCTL" }, { 0xc035, "SHADOW" },
	{ 0xc036, "FPIREG/CYAREG" }, { 0xc037, "BMAREG" }, { 0xc038, "SCCBREG" }, { 0xc039, "SCCAREG" },
	{ 0xc03a, "SCCBDATA" }, { 0xc03b, "SCCADATA" }, { 0xc03c, "SOUNDCTL" }, { 0xc03d, "SOUNDDATA" },
	{ 0xc03e, "SOUNDADRL" }, { 0xc03f, "SOUNDADRH" }, { 0xc040, "STROBE/RDXYMSK" }, { 0xc041, "RDVBLMSK" },
	{ 0xc042, "RDX0EDGE" }, { 0xc043, "RDY0EDGE" }, { 0xc044, "MMDELTAX" }, { 0xc045, "MMDELTAY" },
	{ 0xc046, "DIAGTYPE" }, { 0xc047, "CLRVBLINT" }, { 0xc048, "CLRXYINT" }, { 0xc04f, "EMUBYTE" },
	{ 0xc050, "TXTCLR" }, { 0xc051, "TXTSET" },
	{ 0xc052, "MIXCLR" }, { 0xc053, "MIXSET" }, { 0xc054, "TXTPAGE1" }, { 0xc055, "TXTPAGE2" },
	{ 0xc056, "LORES" }, { 0xc057, "HIRES" }, { 0xc058, "CLRAN0" }, { 0xc059, "SETAN0" },
	{ 0xc05a, "CLRAN1" }, { 0xc05b, "SETAN1" }, { 0xc05c, "CLRAN2" }, { 0xc05d, "SETAN2" },
	{ 0xc05e, "DHIRESON" }, { 0xc05f, "DHIRESOFF" }, { 0xc060, "TAPEIN" }, { 0xc061, "RDBTN0" },
	{ 0xc062, "BUTN1" }, { 0xc063, "RD63" }, { 0xc064, "PADDL0" }, { 0xc065, "PADDL1" },
	{ 0xc066, "PADDL2" }, { 0xc067, "PADDL3" }, { 0xc068, "STATEREG" }, { 0xc070, "PTRIG" }, { 0xc073, "BANKSEL" },
	{ 0xc07e, "IOUDISON" }, { 0xc07f, "IOUDISOFF" }, { 0xc081, "ROMIN" }, { 0xc083, "LCBANK2" },
	{ 0xc085, "ROMIN" }, { 0xc087, "LCBANK2" }, { 0xcfff, "DISCC8ROM" },

	{ 0xF800, "F8ROM:PLOT" }, { 0xF80E, "F8ROM:PLOT1" } , { 0xF819, "F8ROM:HLINE" }, { 0xF828, "F8ROM:VLINE" },
	{ 0xF832, "F8ROM:CLRSCR" }, { 0xF836, "F8ROM:CLRTOP" }, { 0xF838, "F8ROM:CLRSC2" }, { 0xF847, "F8ROM:GBASCALC" },
	{ 0xF856, "F8ROM:GBCALC" }, { 0xF85F, "F8ROM:NXTCOL" }, { 0xF864, "F8ROM:SETCOL" }, { 0xF871, "F8ROM:SCRN" },
	{ 0xF882, "F8ROM:INSDS1" }, { 0xF88E, "F8ROM:INSDS2" }, { 0xF8A5, "F8ROM:ERR" }, { 0xF8A9, "F8ROM:GETFMT" },
	{ 0xF8D0, "F8ROM:INSTDSP" }, { 0xF940, "F8ROM:PRNTYX" }, { 0xF941, "F8ROM:PRNTAX" }, { 0xF944, "F8ROM:PRNTX" },
	{ 0xF948, "F8ROM:PRBLNK" }, { 0xF94A, "F8ROM:PRBL2" },  { 0xF84C, "F8ROM:PRBL3" }, { 0xF953, "F8ROM:PCADJ" },
	{ 0xF854, "F8ROM:PCADJ2" }, { 0xF856, "F8ROM:PCADJ3" }, { 0xF85C, "F8ROM:PCADJ4" }, { 0xF962, "F8ROM:FMT1" },
	{ 0xF9A6, "F8ROM:FMT2" }, { 0xF9B4, "F8ROM:CHAR1" }, { 0xF9BA, "F8ROM:CHAR2" }, { 0xF9C0, "F8ROM:MNEML" },
	{ 0xFA00, "F8ROM:MNEMR" }, { 0xFA40, "F8ROM:OLDIRQ" }, { 0xFA4C, "F8ROM:BREAK" }, { 0xFA59, "F8ROM:OLDBRK" },
	{ 0xFA62, "F8ROM:RESET" }, { 0xFAA6, "F8ROM:PWRUP" }, { 0xFABA, "F8ROM:SLOOP" }, { 0xFAD7, "F8ROM:REGDSP" },
	{ 0xFADA, "F8ROM:RGDSP1" }, { 0xFAE4, "F8ROM:RDSP1" }, { 0xFB19, "F8ROM:RTBL" }, { 0xFB1E, "F8ROM:PREAD" },
	{ 0xFB21, "F8ROM:PREAD4" }, { 0xFB25, "F8ROM:PREAD2" }, { 0xFB2F, "F8ROM:INIT" }, { 0xFB39, "F8ROM:SETTXT" },
	{ 0xFB40, "F8ROM:SETGR" }, { 0xFB4B, "F8ROM:SETWND" }, { 0xFB51, "F8ROM:SETWND2" }, { 0xFB5B, "F8ROM:TABV" },
	{ 0xFB60, "F8ROM:APPLEII" }, { 0xFB6F, "F8ROM:SETPWRC" }, { 0xFB78, "F8ROM:VIDWAIT" }, { 0xFB88, "F8ROM:KBDWAIT" },
	{ 0xFBB3, "F8ROM:VERSION" }, { 0xFBBF, "F8ROM:ZIDBYTE2" }, { 0xFBC0, "F8ROM:ZIDBYTE" }, { 0xFBC1, "F8ROM:BASCALC" },
	{ 0xFBD0, "F8ROM:BSCLC2" }, { 0xFBDD, "F8ROM:BELL1" }, { 0xFBE2, "F8ROM:BELL1.2" }, { 0xFBE4, "F8ROM:BELL2" },
	{ 0xFBF0, "F8ROM:STORADV" }, { 0xFBF4, "F8ROM:ADVANCE" }, { 0xFBFD, "F8ROM:VIDOUT" }, { 0xFC10, "F8ROM:BS" },
	{ 0xFC1A, "F8ROM:UP" }, { 0xFC22, "F8ROM:VTAB" }, { 0xFC24, "F8ROM:VTABZ" }, { 0xFC42, "F8ROM:CLREOP" },
	{ 0xFC46, "F8ROM:CLEOP1" }, { 0xFC58, "F8ROM:HOME" }, { 0xFC62, "F8ROM:CR" }, { 0xFC66, "F8ROM:LF" },
	{ 0xFC70, "F8ROM:SCROLL" }, { 0xFC95, "F8ROM:SCRL3" }, { 0xFC9C, "F8ROM:CLREOL" }, { 0xFC9E, "F8ROM:CLREOLZ" },
	{ 0xFCA8, "F8ROM:WAIT" }, { 0xFCB4, "F8ROM:NXTA4" }, { 0xFCBA, "F8ROM:NXTA1" }, { 0xFCC9, "F8ROM:HEADR" },
	{ 0xFCEC, "F8ROM:RDBYTE" }, { 0xFCEE, "F8ROM:RDBYT2" }, { 0xFCFA, "F8ROM:RD2BIT" }, { 0xFD0C, "F8ROM:RDKEY" },
	{ 0xFD18, "F8ROM:RDKEY1" }, { 0xFD1B, "F8ROM:KEYIN" }, { 0xFD2F, "F8ROM:ESC" }, { 0xFD35, "F8ROM:RDCHAR" },
	{ 0xFD3D, "F8ROM:NOTCR" }, { 0xFD62, "F8ROM:CANCEL" }, { 0xFD67, "F8ROM:GETLNZ" }, { 0xFD6A, "F8ROM:GETLN" },
	{ 0xFD6C, "F8ROM:GETLN0" }, { 0xFD6F, "F8ROM:GETLN1" }, { 0xFD8B, "F8ROM:CROUT1" }, { 0xFD8E, "F8ROM:CROUT" },
	{ 0xFD92, "F8ROM:PRA1" }, { 0xFDA3, "F8ROM:XAM8" }, { 0xFDDA, "F8ROM:PRBYTE" }, { 0xFDE3, "F8ROM:PRHEX" },
	{ 0xFDE5, "F8ROM:PRHEXZ" }, { 0xFDED, "F8ROM:COUT" }, { 0xFDF0, "F8ROM:COUT1" }, { 0xFDF6, "F8ROM:COUTZ" },
	{ 0xFE18, "F8ROM:SETMODE" }, { 0xFE1F, "F8ROM:IDROUTINE" }, { 0xFE20, "F8ROM:LT" }, { 0xFE22, "F8ROM:LT2" },
	{ 0xFE2C, "F8ROM:MOVE" }, { 0xFE36, "F8ROM:VFY" }, { 0xFE5E, "F8ROM:LIST" }, { 0xFE63, "F8ROM:LIST2" },
	{ 0xFE75, "F8ROM:A1PC" }, { 0xFE80, "F8ROM:SETINV" }, { 0xFE84, "F8ROM:SETNORM" }, { 0xFE89, "F8ROM:SETKBD" },
	{ 0xFE8B, "F8ROM:INPORT" }, { 0xFE8D, "F8ROM:INPRT" }, { 0xFE93, "F8ROM:SETVID" }, { 0xFE95, "F8ROM:OUTPORT" },
	{ 0xFE97, "F8ROM:OUTPRT" }, { 0xFEB0, "F8ROM:XBASIC" }, { 0xFEB3, "F8ROM:BASCONT" }, { 0xFEB6, "F8ROM:GO" },
	{ 0xFECA, "F8ROM:USR" }, { 0xFECD, "F8ROM:WRITE" }, { 0xFEFD, "F8ROM:READ" }, { 0xFF2D, "F8ROM:PRERR" },
	{ 0xFF3A, "F8ROM:BELL" }, { 0xFF3F, "F8ROM:RESTORE" }, { 0xFF4A, "F8ROM:SAVE" }, { 0xFF58, "F8ROM:IORTS" },
	{ 0xFF59, "F8ROM:OLDRST" }, { 0xFF65, "F8ROM:MON" }, { 0xFF69, "F8ROM:MONZ" }, { 0xFF6C, "F8ROM:MONZ2" },
	{ 0xFF70, "F8ROM:MONZ4" }, { 0xFF8A, "F8ROM:DIG" }, { 0xFFA7, "F8ROM:GETNUM" }, { 0xFFAD, "F8ROM:NXTCHR" },
	{ 0xFFBE, "F8ROM:TOSUB" }, { 0xFFC7, "F8ROM:ZMODE" }, { 0xFFCC, "F8ROM:CHRTBL" }, { 0xFFE3, "F8ROM:SUBTBL" },

	{ 0xffff, "" }
};

static const struct dasm_data32 gs_vectors[] =
{
	{ 0xE10000, "System Tool dispatcher" }, { 0xE10004, "System Tool dispatcher, glue entry" }, { 0xE10008, "User Tool dispatcher" }, { 0xE1000C, "User Tool dispatcher, glue entry" },
	{ 0xE10010, "Interrupt mgr" }, { 0xE10014, "COP mgr" }, { 0xE10018, "Abort mgr" }, { 0xE1001C, "System Death mgr" }, { 0xE10020, "AppleTalk interrupt" },
	{ 0xE10024, "Serial interrupt" }, { 0xE10028, "Scanline interrupt" }, { 0xE1002C, "Sound interrupt" }, { 0xE10030, "VertBlank interrupt" }, { 0xE10034, "Mouse interrupt" },
	{ 0xE10038, "1/4 sec interrupt" }, { 0xE1003C, "Keyboard interrupt" }, { 0xE10040, "ADB Response byte int" }, { 0xE10044, "ADB SRQ int" }, { 0xE10048, "Desk Acc mgr" },
	{ 0xE1004C, "FlushBuffer handler" }, { 0xE10050, "KbdMicro interrupt" }, { 0xE10054, "1 sec interrupt" }, { 0xE10058, "External VGC int" }, { 0xE1005C, "other interrupt" },
	{ 0xE10060, "Cursor update" }, { 0xE10064, "IncBusy" }, { 0xE10068, "DecBusy" }, { 0xE1006C, "Bell vector" }, { 0xE10070, "Break vector" }, { 0xE10074, "Trace vector" },
	{ 0xE10078, "Step vector" }, { 0xE1007C, "[install ROMdisk]" }, { 0xE10080, "ToWriteBram" }, { 0xE10084, "ToReadBram" }, { 0xE10088, "ToWriteTime" },
	{ 0xE1008C, "ToReadTime" }, { 0xE10090, "ToCtrlPanel" }, { 0xE10094, "ToBramSetup" }, { 0xE10098, "ToPrintMsg8" }, { 0xE1009C, "ToPrintMsg16" }, { 0xE100A0, "Native Ctrl-Y" },
	{ 0xE100A4, "ToAltDispCDA" }, { 0xE100A8, "ProDOS 16 [inline parms]" }, { 0xE100AC, "OS vector" }, { 0xE100B0, "GS/OS(@parms,call) [stackmode parms]" },
	{ 0xE100B4, "OS_P8_Switch" }, { 0xE100B8, "OS_Public_Flags" }, { 0xE100BC, "OS_KIND (byte: 0=P8,1=P16)" }, { 0xE100BD, "OS_BOOT (byte)" }, { 0xE100BE, "OS_BUSY (bit 15=busy)" },
	{ 0xE100C0, "MsgPtr" }, { 0xe10135, "CURSOR" }, { 0xe10136, "NXTCUR" },
	{ 0xE10180, "ToBusyStrip" }, { 0xE10184, "ToStrip" }, { 0xe10198, "MDISPATCH" }, { 0xe1019c, "MAINSIDEPATCH" },
	{ 0xE101B2, "MidiInputPoll" }, { 0xE10200, "Memory Mover" }, { 0xE10204, "Set System Speed" },
	{ 0xE10208, "Slot Arbiter" }, { 0xE10220, "HyperCard IIgs callback" }, { 0xE10224, "WordForRTL" }, { 0xE11004, "ATLK: BASIC" }, { 0xE11008, "ATLK: Pascal" },
	{ 0xE1100C, "ATLK: RamGoComp" }, { 0xE11010, "ATLK: SoftReset" }, { 0xE11014, "ATLK: RamDispatch" }, { 0xE11018, "ATLK: RamForbid" }, { 0xE1101C, "ATLK: RamPermit" },
	{ 0xE11020, "ATLK: ProEntry" }, { 0xE11022, "ATLK: ProDOS" }, { 0xE11026, "ATLK: SerStatus" }, { 0xE1102A, "ATLK: SerWrite" }, { 0xE1102E, "ATLK: SerRead" },
	{ 0xE1103A, "ATLK: InitFileHook" }, { 0xE1103E, "ATLK: PFI Vector" }, { 0xE1D600, "ATLK: CmdTable" }, { 0xE1DA00, "ATLK: TickCount" },
	{ 0xE01D00, "BRegSave" }, { 0xE01D02, "IntStatus" }, { 0xE01D03, "SVStateReg" }, { 0xE01D04, "80ColSave" }, { 0xE01D05, "LoXClampSave" },
	{ 0xE01D07, "LoYClampSave" }, { 0xE01D09, "HiXClampSave" }, { 0xE01D0B, "HiYClampSave" }, { 0xE01D0D, "OutGlobals" }, { 0xE01D14, "Want40" },
	{ 0xE01D16, "CursorSave" }, { 0xE01D18, "NEWVIDSave" }, { 0xE01D1A, "TXTSave" }, { 0xE01D1B, "MIXSave" }, { 0xE01D1C, "PAGE2Save" },
	{ 0xE01D1D, "HIRESSave" }, { 0xE01D1E, "ALTCHARSave" }, { 0xE01D1F, "VID80Save" }, { 0xE01D20, "Int1AY" }, { 0xE01D2D, "Int1BY" },
	{ 0xE01D39, "Int2AY" }, { 0xE01D4C, "Int2BY" }, { 0xE01D61, "MOUSVBLSave" }, { 0xE01D63, "DirPgSave" }, { 0xE01D65, "C3ROMSave" },
	{ 0xE01D66, "Save4080" }, { 0xE01D67, "NumInts" }, { 0xE01D68, "MMode" }, { 0xE01D6A, "MyMSLOT" }, { 0xE01D6C, "Slot" },
	{ 0xE01D6E, "EntryCount" }, { 0xE01D70, "BottomLine" }, { 0xE01D72, "HPos" }, { 0xE01D74, "VPos" }, { 0xE01D76, "CurScreenLoc" },
	{ 0xE01D7C, "NumDAs" }, { 0xE01D7E, "LeftBorder" }, { 0xE01D80, "FirstMenuItem" }, { 0xE01D82, "IDNum" }, { 0xE01D84, "CDATabHndl" },
	{ 0xE01D88, "RoomLeft" }, { 0xE01D8A, "KeyInput" }, { 0xE01D8C, "EvntRec" }, { 0xE01D8E, "Message" }, { 0xE01D92, "When" },
	{ 0xE01D96, "Where" }, { 0xE01D9A, "Mods" }, { 0xE01D9C, "StackSave" }, { 0xE01D9E, "OldOutGlobals" }, { 0xE01DA2, "OldOutDevice" },
	{ 0xE01DA8, "CDataBPtr" }, { 0xE01DAC, "DAStrPtr" }, { 0xE01DB0, "CurCDA" }, { 0xE01DB2, "OldOutHook" }, { 0xE01DB4, "OldInDev" },
	{ 0xE01DBA, "OldInGlob" }, { 0xE01DBE, "RealDeskStat" }, { 0xE01DC0, "Next" }, { 0xE01DDE, "SchActive" }, { 0xE01DDF, "TaskQueue" },
	{ 0xE01DDF, "FirstTask" }, { 0xE01DE3, "SecondTask" }, { 0xE01DED, "Scheduler" }, { 0xE01DEF, "Offset" }, { 0xE01DFF, "Lastbyte" },
	{ 0xE01E04, "QD:StdText" }, { 0xE01E08, "QD:StdLine" }, { 0xE01E0C, "QD:StdRect" }, { 0xE01E10, "QD:StdRRect" }, { 0xE01E14, "QD:StdOval" }, { 0xE01E18, "QD:StdArc" }, { 0xE01E1C, "QD:StdPoly" },
	{ 0xE01E20, "QD:StdRgn" }, { 0xE01E24, "QD:StdPixels" }, { 0xE01E28, "QD:StdComment" }, { 0xE01E2C, "QD:StdTxMeas" }, { 0xE01E30, "QD:StdTxBnds" }, { 0xE01E34, "QD:StdGetPic" },
	{ 0xE01E38, "QD:StdPutPic" }, { 0xE01E98, "QD:ShieldCursor" }, { 0xE01E9C, "QD:UnShieldCursor" },
	{ 0x010100, "MNEMSTKPTR" }, { 0x010101, "ALEMSTKPTR" }, { 0x01FC00, "SysSrv:DEV_DISPATCHER" }, { 0x01FC04, "SysSrv:CACHE_FIND_BLK" }, { 0x01FC08, "SysSrv:CACHE_ADD_BLK" },
	{ 0x01FC0C, "SysSrv:CACHE_INIT" }, { 0x01FC10, "SysSrv:CACHE_SHUTDN" }, { 0x01FC14, "SysSrv:CACHE_DEL_BLK" }, { 0x01FC18, "SysSrv:CACHE_DEL_VOL" },
	{ 0x01FC1C, "SysSrv:ALLOC_SEG" }, { 0x01FC20, "SysSrv:RELEASE_SEG" }, { 0x01FC24, "SysSrv:ALLOC_VCR" }, { 0x01FC28, "SysSrv:RELEASE_VCR" },
	{ 0x01FC2C, "SysSrv:ALLOC_FCR" }, { 0x01FC30, "SysSrv:RELEASE_FCR" }, { 0x01FC34, "SysSrv:SWAP_OUT" }, { 0x01FC38, "SysSrv:DEREF" },
	{ 0x01FC3C, "SysSrv:GET_SYS_GBUF" }, { 0x01FC40, "SysSrv:SYS_EXIT" }, { 0x01FC44, "SysSrv:SYS_DEATH" }, { 0x01FC48, "SysSrv:FIND_VCR" },
	{ 0x01FC4C, "SysSrv:FIND_FCR" }, { 0x01FC50, "SysSrv:SET_SYS_SPEED" }, { 0x01FC54, "SysSrv:CACHE_FLSH_DEF" }, { 0x01FC58, "SysSrv:RENAME_VCR" },
	{ 0x01FC5C, "SysSrv:RENAME_FCR" }, { 0x01FC60, "SysSrv:GET_VCR" }, { 0x01FC64, "SysSrv:GET_FCR" }, { 0x01FC68, "SysSrv:LOCK_MEM" },
	{ 0x01FC6C, "SysSrv:UNLOCK_MEM" }, { 0x01FC70, "SysSrv:MOVE_INFO" }, { 0x01FC74, "SysSrv:CVT_0TO1" }, { 0x01FC78, "SysSrv:CVT_1TO0" },
	{ 0x01FC7C, "SysSrv:REPLACE80" }, { 0x01FC80, "SysSrv:TO_B0_CORE" }, { 0x01FC84, "SysSrv:G_DISPATCH" }, { 0x01FC88, "SysSrv:SIGNAL" },
	{ 0x01FC8C, "SysSrv:GET_SYS_BUFF" }, { 0x01FC90, "SysSrv:SET_DISK_SW" }, { 0x01FC94, "SysSrv:REPORT_ERROR" }, { 0x01FC98, "SysSrv:MOUNT_MESSAGE" },
	{ 0x01FC9C, "SysSrv:FULL_ERROR" }, { 0x01FCA0, "SysSrv:RESERVED_07" }, { 0x01FCA4, "SysSrv:SUP_DRVR_DISP" }, { 0x01FCA8, "SysSrv:INSTALL_DRIVER" },
	{ 0x01FCAC, "SysSrv:S_GET_BOOT_PFX" },  { 0x01FCB0, "SysSrv:S_SET_BOOT_PFX" }, { 0x01FCB4, "SysSrv:LOW_ALLOCATE" },
	{ 0x01FCB8, "SysSrv:GET_STACKED_ID" }, { 0x01FCBC, "SysSrv:DYN_SLOT_ARBITER" }, { 0x01FCC0, "SysSrv:PARSE_PATH" },
	{ 0x01FCC4, "SysSrv:OS_EVENT" }, { 0x01FCC8, "SysSrv:INSERT_DRIVER" }, { 0x01FCCC, "SysSrv:(device manager?)" },
	{ 0x01FCD0, "SysSrv:Old Device Dispatcher" }, { 0x01FCD4, "SysSrv:INIT_PARSE_PATH" }, { 0x01FCD8, "SysSrv:UNBIND_INT_VEC" },
	{ 0x01FCDC, "SysSrv:DO_INSERT_SCAN" }, { 0x01FCE0, "SysSrv:TOOLBOX_MSG" },

	{ 0xffff, "" }
};


void DumpInstruction() {

	std::string log = "{0:02X}:{1:04X}: ";
	const char* f = "";
	const char* sta;

	instruction_type type = implied;
	operand_type opType = none;

	std::string arg1 = "";
	std::string arg2 = "";

	switch (ins_in[0])
	{
	case 0x00: sta = "brk"; break;
	case 0x98: sta = "tya"; break;
	case 0xA8: sta = "tay"; break;
	case 0xAA: sta = "tax"; break;
	case 0x8A: sta = "txa"; break;
	case 0x9B: sta = "txy"; break;
	case 0x40: sta = "rti"; break;
	case 0x60: sta = "rts"; break;
	case 0x9A: sta = "txs"; break;
	case 0xBA: sta = "tsx"; break;
	case 0xBB: sta = "tyx"; break;
	case 0x0C: sta = "tsb"; type = absolute; opType = byte3; break;
	case 0x1B: sta = "tcs"; break;
	case 0x5B: sta = "tcd"; break;

	case 0x08: sta = "php"; break;
	case 0x0B: sta = "phd"; break;
	case 0x2B: sta = "pld"; break;
	case 0xAB: sta = "plb"; break;
	case 0x8B: sta = "phb"; break;
	case 0x4B: sta = "phk"; break;
	case 0x28: sta = "plp"; break;
	case 0xfb: sta = "xce"; break;

	case 0x18: sta = "clc"; break;
	case 0x58: sta = "cli"; break;
	case 0xB8: sta = "clv"; break;
	case 0xD8: sta = "cld"; break;

	case 0xE8: sta = "inx"; break;
	case 0xC8: sta = "iny"; break;
	case 0x1A: sta = "ina"; break;

	case 0x70: sta = "bvs"; type = relativeLong; break;
	case 0x80: sta = "bra"; type = relativeLong; break;

	case 0x38: sta = "sec"; break;
	case 0xe2: sta = "sep"; type = immediate;  break;
	case 0x78: sta = "sei"; break;
	case 0xF8: sta = "sed"; break;

	case 0x48: sta = "pha"; break;
	case 0xDA: sta = "phx"; break;
	case 0x5A: sta = "phy"; break;
	case 0x68: sta = "pla"; break;
	case 0xFA: sta = "plx"; break;
	case 0x7A: sta = "ply"; break;

	case 0xF4: sta = "pea"; type = absolute; break;
	case 0x62: sta = "per"; type = relativeLong; break;
	case 0xD4: sta = "pei"; type = zeroPage; break;

	case 0x0A: sta = "asl"; type = accumulator; break;
	case 0x06: sta = "asl"; type = zeroPage; break;
	case 0x16: sta = "asl"; type = zeroPageX; break;
	case 0x0E: sta = "asl"; type = absolute; break;
	case 0x1E: sta = "asl"; type = absoluteX; break;

	case 0x01: sta = "ora"; type = indirectX; break;
	case 0x03: sta = "ora"; type = stackmode; break;
	case 0x05: sta = "ora"; type = zeroPage; break;
	case 0x07: sta = "ora"; type = direct24; break;
	case 0x09: sta = "ora"; type = immediate; break;
	case 0x0D: sta = "ora"; type = absolute; opType = byte2; break;
	case 0x0F: sta = "ora"; type = longValue; opType = byte3; break;
	case 0x11: sta = "ora"; type = indirectY; break;
	case 0x15: sta = "ora"; type = zeroPageX; break;
	case 0x17: sta = "ora"; type = direct24Y; break;
	case 0x19: sta = "ora"; type = absoluteY; break;
	case 0x1D: sta = "ora"; type = absoluteX; break;
	case 0x1F: sta = "ora"; type = longX; break;

	case 0x43: sta = "eor"; type = stackmode; break;
	case 0x47: sta = "eor"; type = direct24; break;
	case 0x49: sta = "eor"; type = immediate; break;
	case 0x4d: sta = "eor"; type = absolute; break;
	case 0x45: sta = "eor"; type = zeroPage; break;
	case 0x55: sta = "eor"; type = zeroPageX; break;
	case 0x57: sta = "eor"; type = direct24Y; break;
	case 0x5d: sta = "eor"; type = absoluteX; break;
	case 0x59: sta = "eor"; type = absoluteY; break;
	case 0x41: sta = "eor"; type = indirectX; break;
	case 0x51: sta = "eor"; type = indirectY; break;

	case 0x23: sta = "and"; type = stackmode; break;
	case 0x25: sta = "and"; type = zeroPage; break;
	case 0x27: sta = "and"; type = direct24; break;
	case 0x29: sta = "and"; type = immediate; break;
	case 0x2D: sta = "and"; type = absolute; break;
	case 0x35: sta = "and"; type = zeroPageX; break;
	case 0x37: sta = "and"; type = direct24Y; break;
	case 0x39: sta = "and"; type = absoluteY; break;
	case 0x3D: sta = "and"; type = absoluteX; break;


	case 0xE1: sta = "sbc"; type = indirectX; break;
	case 0xE3: sta = "sbc"; type = stackmode; break;
	case 0xE5: sta = "sbc"; type = zeroPage; break;
	case 0xE7: sta = "sbc"; type = direct24; break;
	case 0xE9: sta = "sbc"; type = immediate; break;
	case 0xED: sta = "sbc"; type = absolute; break;
	case 0xF1: sta = "sbc"; type = indirectY; break;
	case 0xF5: sta = "sbc"; type = zeroPageX; break;
	case 0xF7: sta = "sbc"; type = direct24Y; break;
	case 0xF9: sta = "sbc"; type = absoluteY; break;
	case 0xFD: sta = "sbc"; type = absoluteX; break;

	case 0xC3: sta = "cmp"; type = stackmode; break;
	case 0xC5: sta = "cmp"; type = zeroPage; break;
	case 0xC7: sta = "cmp"; type = direct24; break;
	case 0xC9: sta = "cmp"; type = immediate; break;
	case 0xCD: sta = "cmp"; type = absolute; break;
	case 0xCF: sta = "cmp"; type = longValue; opType=byte3; break;
	case 0xD1: sta = "cmp"; type = indirectY; break;
	case 0xD5: sta = "cmp"; type = zeroPageX; break;
	case 0xD7: sta = "cmp"; type = direct24Y; break;
	case 0xD9: sta = "cmp"; type = absoluteY; break;
	case 0xDD: sta = "cmp"; type = absoluteX; break;
	case 0xDF: sta = "cmp"; type = longX; break;


	case 0xE0: sta = "cpx"; type = immediate; break;
	case 0xE4: sta = "cpx"; type = zeroPage; break;
	case 0xEC: sta = "cpx"; type = absolute; break;

	case 0xC0: sta = "cpy"; type = immediate; break;
	case 0xC4: sta = "cpy"; type = zeroPage; break;
	case 0xCC: sta = "cpy"; type = absolute; break;

	case 0xC2: sta = "rep"; type = immediate; break;

	case 0xA2: sta = "ldx"; type = immediate; break;
	case 0xA6: sta = "ldx"; type = zeroPage; break;
	case 0xB6: sta = "ldx"; type = zeroPageY; break;
	case 0xAE: sta = "ldx"; type = absolute; break;
	case 0xBE: sta = "ldx"; type = absoluteY; break;

	case 0xA0: sta = "ldy"; type = immediate; break;
	case 0xA4: sta = "ldy"; type = zeroPage; break;
	case 0xB4: sta = "ldy"; type = zeroPageX; break;
	case 0xAC: sta = "ldy"; type = absolute; break;
	case 0xBC: sta = "ldy"; type = absoluteX; break;

	case 0xA1: sta = "lda"; type = indirectX; break;
	case 0xA3: sta = "lda"; type = stackmode; break;
	case 0xA5: sta = "lda"; type = zeroPage; break;
	case 0xA7: sta = "lda"; type = direct24; break;
	case 0xA9: sta = "lda"; type = immediate; break;
	case 0xAD: sta = "lda"; type = absolute; opType = byte3; break;
	case 0xAF: sta = "lda"; type = longValue; opType=byte3; break;
	case 0xB1: sta = "lda"; type = indirectY; break;
	case 0xB2: sta = "lda"; type = indirect; break;
	case 0xB5: sta = "lda"; type = zeroPageX; break;
	case 0xB7: sta = "lda"; type = direct24Y; break;
	case 0xB9: sta = "lda"; type = absoluteY; break;
	case 0xBD: sta = "lda"; type = absoluteX; break;
	case 0xBF: sta = "lda"; type = longX; break;


	case 0x1C: sta = "trb"; type = absolute; break;

	case 0x81: sta = "sta"; type = indirectX; break;
	case 0x83: sta = "sta"; type = stackmode; break;
	case 0x85: sta = "sta"; type = zeroPage; break;
	case 0x87: sta = "sta"; type = direct24; break;
	case 0x8D: sta = "sta"; type = absolute; opType = byte3; break;
	case 0x8F: sta = "sta"; type = longValue; opType = byte3; break;
	case 0x91: sta = "sta"; type = indirectY; break;
	case 0x95: sta = "sta"; type = zeroPageX; break;
	case 0x97: sta = "sta"; type = direct24Y; break;
	case 0x99: sta = "sta"; type = absoluteY; break;
	case 0x9D: sta = "sta"; type = absoluteX; break;
	case 0x9F: sta = "sta"; type = longX; break;


	case 0x86: sta = "stx"; type = zeroPage; break;
	case 0x96: sta = "stx"; type = zeroPageY; break;
	case 0x8E: sta = "stx"; type = absolute; break;
	case 0x84: sta = "sty"; type = zeroPage; break;
	case 0x94: sta = "sty"; type = zeroPageX; break;
	case 0x8C: sta = "sty"; type = absolute; break;
	case 0x64: sta = "stz"; type = zeroPage;  break;
	case 0x9C: sta = "stz"; type = absolute;  opType = byte3; break;
	case 0x9E: sta = "stz"; type = absoluteX; break;

	case 0x63: sta = "adc"; type = stackmode; break;
	case 0x65: sta = "adc"; type = zeroPage; break;
	case 0x67: sta = "adc"; type = direct24; break;
	case 0x69: sta = "adc"; type = immediate; break;
	case 0x6D: sta = "adc"; type = absolute; break;
	case 0x75: sta = "adc"; type = zeroPageX; break;
	case 0x77: sta = "adc"; type = direct24Y; break;
	case 0x79: sta = "adc"; type = absoluteY; break;
	case 0x7D: sta = "adc"; type = absoluteX; break;

	case 0x3b: sta = "tsc"; break;
	case 0x7b: sta = "tdc"; break;

	case 0xC6: sta = "dec"; type = zeroPage;  break;
	case 0xD6: sta = "dec"; type = zeroPageX;  break;
	case 0xCE: sta = "dec"; type = absolute;  break;
	case 0xDE: sta = "dec"; type = absoluteX;  break;

	case 0x3A: sta = "dea"; break;
	case 0xCA: sta = "dex"; break;
	case 0x88: sta = "dey"; break;

	case 0xEB: sta = "xba"; break;

	case 0x24: sta = "bit"; type = zeroPage; break;
	case 0x2C: sta = "bit"; type = absolute; break;
	case 0x3C: sta = "bit"; type = absoluteX; break;
	case 0x89: sta = "bit"; type = immediate; break;

	case 0x30: sta = "bmi"; type = relativeLong; break;
	case 0x90: sta = "bcc"; type = relative; break;
	case 0xB0: sta = "bcs"; type = relative; break;
	case 0xD0: sta = "bne"; type = relative; break;
	case 0xF0: sta = "beq"; type = relative; break;
	case 0x50: sta = "bvc"; type = relative; break;
	case 0x10: sta = "bpl"; type = relative; break;

	case 0x26: sta = "rol"; type = zeroPage; break;
	case 0x2a: sta = "rol"; type = accumulator; break;
	case 0x2e: sta = "rol"; type = absolute ; break;
	case 0x3e: sta = "rol"; type = absoluteX; break;

	case 0x66: sta = "ror"; type = zeroPage; break;
	case 0x6a: sta = "ror"; type = accumulator; break;
	case 0x6e: sta = "ror"; type = absolute ; break;
	case 0x7e: sta = "ror"; type = absoluteX; break;

	case 0x46: sta = "lsr"; type = zeroPage; break;
	case 0x4A: sta = "lsr"; type = accumulator; break;
	case 0x4e: sta = "lsr"; type = absolute ; break;
	case 0x5e: sta = "lsr"; type = absoluteX; break;

	case 0x54: sta = "mvn"; type = srcdst; break;
	case 0x44: sta = "mvp"; type = srcdst; break;

	case 0xE6: sta = "inc"; type = zeroPage; break;
	case 0xF6: sta = "inc"; type = zeroPageX; break;
	case 0xEE: sta = "inc"; type = absolute; break;
	case 0xFE: sta = "inc"; type = absoluteX; break;

	case 0x20: sta = "jsr"; type = absolute; opType = byte3; break;
	case 0xFC: sta = "jsr"; type = absoluteX; break;

	case 0x22: sta = "jsl"; type = longValue; opType = byte3; break;

	case 0x4C: sta = "jmp"; type = absolute; break;
	case 0x5C: sta = "jmp"; type = longValue; opType=byte3; break;
	case 0x6C: sta = "jmp"; type = indirect; break;
	case 0x7C: sta = "jmp"; type = absoluteX; break;

	case 0x6B: sta = "rtl";  break;

	case 0xEA: sta = "nop";  break;

	default: sta = "???";  f = "\t\tPC={0:X} arg1={1:X} arg2={2:X} IN0={3:X} IN1={4:X} IN2={5:X} IN3={6:X} IN4={7:X} MA0={8:X} MA1={9:X} MA2={10:X} MA3={11:X} MA4={12:X}";
	}

	// replace out named values?

	if (ins_index > 1) {

		if (opType == byte3) {
			unsigned long operand = ins_in[1];
			operand |= (ins_in[2] << 8);
			operand |= (ins_in[3] << 16);

			if (ins_index <= 3) {
				operand = ins_in[1];
				operand |= (ins_in[2] << 8);
				operand |= (ins_dbr[1] << 16);
			}
			//console.AddLog("%d %x", ins_index, operand);

			int item = 0;
			while (gs_vectors[item].addr != 0xffff)
			{
				if (gs_vectors[item].addr == operand)
				{
					ins_str[1] = type == longValue ? ">" : "";
					ins_str[1].append(gs_vectors[item].name);
					type = formatted;
					break;
				}
				item++;
			}
		}

		if (type != formatted && (opType == byte2 || (opType == byte3 && ins_index == 3))) {

			unsigned short operand = ins_in[1];
			if (ins_index > 2) {
				operand |= (ins_in[2] << 8);
			}

			int item = 0;
			while (a2_stuff[item].addr != 0xffff)
			{
				if (a2_stuff[item].addr == operand)
				{
					ins_str[1] = a2_stuff[item].name;
					type = formatted;
					break;
				}
				item++;
			}
		}
	}


	f = "{2:s}";
	unsigned long relativeAddress = ins_ma[0] + ((signed char)ins_in[1]) + 2;
	if (sta == "per") {
		relativeAddress++; // I HATE THIS
	}
	unsigned char maHigh0 = (unsigned char)(ins_ma[0] >> 16) & 0xff;
	unsigned char maHigh1 = (unsigned char)(ins_ma[1] >> 16) & 0xff;

	signed char signedIn1 = ins_in[1];
	std::string signedIn1Formatted = signedIn1 < 0 ? fmt::format("-${0:x}", signedIn1 * -1) : fmt::format("${0:x}", signedIn1);

	switch (type) {
	case implied: f = ""; break;
	case formatted: arg1 = ins_str[1]; f = " {2:s}"; break;
	case immediate:
		if (ins_index == 3) {
			arg1 = fmt::format(" #${0:02x}{1:02x}", ins_in[2], ins_in[1]);
		}
		else {
			arg1 = fmt::format(" #${0:02x}", ins_in[1]);
		}
		break;
	case srcdst: arg1 = fmt::format(" ${0:02x}, ${1:02x}", ins_in[2], ins_in[1]); break;
	case absolute: arg1 = fmt::format(" ${0:02x}{1:02x}", ins_in[2], ins_in[1]); break;
	case absoluteX: arg1 = fmt::format(" ${0:02x}{1:02x},x", ins_in[2], ins_in[1]); break;
	case absoluteY: arg1 = fmt::format(" ${0:02x}{1:02x},y", ins_in[2], ins_in[1]); break;
	case zeroPage: arg1 = fmt::format(" ${0:02x}", ins_in[1]); break;
	case direct24: arg1 = fmt::format(" [${0:02x}]", ins_in[1]); break;
	case direct24X: arg1 = fmt::format(" [${0:02x}],x", ins_in[1]); break;
	case direct24Y: arg1 = fmt::format(" [${0:02x}],y", ins_in[1]); break;
	case zeroPageX: arg1 = fmt::format(" ${0:02x},x", ins_in[1]); break;
	case zeroPageY: arg1 = fmt::format(" ${0:02x},y", ins_in[1]); break;
	case indirect: arg1 = fmt::format(" (${0:04x})", ins_in[1]); break;
	case indirectX: arg1 = fmt::format(" (${0:02x}),x", ins_in[1]); break;
	case indirectY: arg1 = fmt::format(" (${0:02x}),y", ins_in[1]); break;
	case stackmode: arg1 = fmt::format(" ${0:x},s", ins_in[1]); break;
	case longValue: arg1 = fmt::format(" ${0:02x}{1:02x}{2:02x}", ins_in[3], ins_in[2], ins_in[1]); break;
	case longX: arg1 = fmt::format(" ${0:02x}{1:02x}{2:02x},x", ins_in[3], ins_in[2], ins_in[1]); break;
	case longY: arg1 = fmt::format(" ${0:02x}{1:02x}{2:02x},y", ins_in[3], ins_in[2], ins_in[1]); break;
		//case longX: arg1 = fmt::format(" ${0:02x}{1:02x}{2:02x},x", maHigh1, ins_in[2], ins_in[1]); break;
		//case longY: arg1 = fmt::format(" ${0:02x}{1:02x}{2:02x},y", maHigh1, ins_in[2], ins_in[1]); break;
	case accumulator: arg1 = "a"; break;
	case relative: arg1 = fmt::format(" {0:06x} ({1})", relativeAddress, signedIn1Formatted);		break;
	case relativeLong: arg1 = fmt::format(" {0:06x} ({1})", relativeAddress, signedIn1Formatted);		break;
	default: arg1 = "UNSUPPORTED TYPE!";
	}

	log.append(sta);
	log.append(f);
	log = fmt::format(log, maHigh0, (unsigned short)ins_pc[0], arg1);

	if (!writeLog(log.c_str())) {
		run_state = RunState::Stopped;
	}
	cpu_instruction_count++;
	//if (sta == "???") {
	//	console.AddLog(log.c_str());
	//	run_enable = 0;
	//}

}

static int last_cpu_addr=-1;
static int already_saw_this = 0;

static uint32_t GetCpuPc()
{
	return ((uint32_t)VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT__M68K__DOT__excUnit__DOT__PcH << 16) |
	       VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT__M68K__DOT__excUnit__DOT__PcL;
}

static uint32_t GetCpuD7()
{
	return ((uint32_t)VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT__M68K__DOT__excUnit__DOT__regs68H[7] << 16) |
	       VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT__M68K__DOT__excUnit__DOT__regs68L[7];
}

static uint32_t GetCpuReg(int reg)
{
	return ((uint32_t)VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT__M68K__DOT__excUnit__DOT__regs68H[reg] << 16) |
	       VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT__M68K__DOT__excUnit__DOT__regs68L[reg];
}

static uint32_t GetCpuA7()
{
	bool supervisor = VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT__M68K__DOT__pswS;
	return GetCpuReg(supervisor ? 16 : 15);
}

static uint32_t SimRamIndex(uint32_t byte_addr);
static uint32_t ReadSimRam32(uint32_t byte_addr);

static bool MaybePatchFullRamTest()
{
	if (!skip_ram_test || ram_test_patch_applied) {
		return true;
	}

	// Wait until the ROM has verified its own checksum. The parity-test region
	// immediately precedes MEMTST2, leaving ample time before the target fetch.
	uint32_t pc = GetCpuPc();
	if (pc < 0xFE0D5C || pc > 0xFE0DF2) {
		return true;
	}

	constexpr unsigned patch_word = 0x0E02 / 2;
	auto& high = VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT__high_ROM_H__DOT__ROM_array;
	auto& low = VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT__low_ROM_H__DOT__ROM_array;
	if (high[patch_word] != 0x32 || low[patch_word] != 0x7c ||
	    high[patch_word + 1] != 0x1e || low[patch_word + 1] != 0x04) {
		fprintf(stderr,
			"skip-ram-test: unexpected H ROM bytes at FE0E02: %02x%02x %02x%02x\n",
			high[patch_word], low[patch_word], high[patch_word + 1], low[patch_word + 1]);
		return false;
	}

	// Replace `MOVEA #MEMSTRT,A1` with `BRA.W TSTDONE` (FE0E4E). This keeps
	// sizing, low-memory validation, status initialization, and the normal
	// post-test success path intact.
	high[patch_word] = 0x60;
	low[patch_word] = 0x00;
	high[patch_word + 1] = 0x00;
	low[patch_word + 1] = 0x48;

	// RAMTEST's final address-check pass leaves every tested longword at
	// 0xffffffff. LOS uses that state at $10000 as its active-low debug-mode
	// flag; skipping the writes without reproducing their final state boots
	// straight into LisaBug. Preserve low ROM workspace and the video page.
	uint32_t screen_base = ReadSimRam32(0x000110);
	if (screen_base < 0x000800 || screen_base > 0x200000) {
		fprintf(stderr, "skip-ram-test: invalid screen base %06x\n", screen_base);
		return false;
	}
	auto& ram = VERTOPINTERN->emu__DOT__core__DOT__slot1__DOT__SDRAM_2MB__DOT__sim_ram;
	for (uint32_t byte_addr = 0x000800; byte_addr < screen_base; byte_addr += 2) {
		ram[SimRamIndex(byte_addr)] = 0xffff;
	}
	ram_test_patch_applied = true;
	fprintf(stderr,
		"skip-ram-test: patched FE0E02 to BRA.W FE0E4E and initialized RAM "
		"through %06x at main_time=%llu pc=%06X\n",
		screen_base, (unsigned long long)main_time, pc);
	return true;
}

static uint32_t SimRamIndex(uint32_t byte_addr)
{
	// The Lisa multiplexes A8:A1 onto the DRAM row and A16:A9 onto the
	// column. SDRAM_Controller_Flat preserves that row/column ordering in
	// its flat backing array rather than using a linear CPU word address.
	return (((byte_addr >> 17) & 0x0f) << 16) |
	       (((byte_addr >> 1) & 0xff) << 8) |
	       ((byte_addr >> 9) & 0xff);
}

static uint32_t SimRamIndexToPhysicalByte(uint32_t index)
{
	return (((index >> 16) & 0x0f) << 17) |
	       (((index >> 8) & 0xff) << 1) |
	       ((index & 0xff) << 9);
}

static uint16_t ReadSimRamWord(uint32_t byte_addr)
{
	return VERTOPINTERN->emu__DOT__core__DOT__slot1__DOT__SDRAM_2MB__DOT__sim_ram[SimRamIndex(byte_addr)];
}

static uint8_t ReadSimRamByte(uint32_t byte_addr)
{
	uint16_t word = ReadSimRamWord(byte_addr);
	return (byte_addr & 1) ? (word & 0xff) : (word >> 8);
}

static uint16_t ReadSimRam16(uint32_t byte_addr)
{
	return ((uint16_t)ReadSimRamByte(byte_addr) << 8) |
	       ReadSimRamByte(byte_addr + 1);
}

static uint32_t ReadSimRam32(uint32_t byte_addr)
{
	return ((uint32_t)ReadSimRam16(byte_addr) << 16) |
	       ReadSimRam16(byte_addr + 2);
}

static void DumpSimRamRange(const char* label, uint32_t start, uint32_t bytes)
{
	fprintf(stderr, "%s @%06X:", label, start);
	for (uint32_t offset = 0; offset < bytes; offset++) {
		if ((offset % 16) == 0) {
			fprintf(stderr, "\n  %06X:", start + offset);
		}
		fprintf(stderr, " %02x", ReadSimRamByte(start + offset));
	}
	fprintf(stderr, "\n");
}

static void DumpMmuSegment(uint8_t segment, unsigned context, uint32_t logical_address)
{
	auto& low = VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT__low_MMU_RAM__DOT__RAM_array;
	auto& mid = VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT__mid_MMU_RAM__DOT__RAM_array;
	auto& high = VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT__high_MMU_RAM__DOT__RAM_array;
	unsigned ms1 = (context & 1) ? 0 : 1;
	unsigned ms2 = (context & 2) ? 0 : 1;
	unsigned base = ((segment & 0x07) << 7) |
	                (((segment >> 4) & 1) << 6) |
	                (((segment >> 5) & 1) << 5) |
	                (((segment >> 6) & 1) << 4) |
	                (ms2 << 3) | (((segment >> 3) & 1) << 2) | ms1;
	auto read_reg = [&](unsigned b_l) {
		unsigned index = base | (b_l << 1);
		return (uint16_t)((high[index] << 8) | (mid[index] << 4) | low[index]);
	};
	uint16_t slr = read_reg(0);
	uint16_t sor = read_reg(1);
	uint32_t segment_base = (uint32_t)segment << 17;
	uint32_t offset = logical_address - segment_base;
	uint32_t physical = ((((uint32_t)sor + (offset >> 9)) & 0x0fff) << 9) |
	                    (offset & 0x01ff);
	fprintf(stderr,
		"MMU segment=%02X context=%u SOR=%03X SLR=%03X logical=%06X physical=%06X\n",
		segment, context, sor, slr, logical_address, physical);
	DumpSimRamRange("MMU-mapped RAM", physical, 64);
}

static void FindSimRamPattern(const char* label, const uint16_t* pattern, size_t words)
{
	unsigned hits = 0;
	fprintf(stderr, "%s:", label);
	for (uint32_t address = 0; address + words * 2 <= 0x200000; address += 2) {
		bool match = true;
		for (size_t i = 0; i < words; i++) {
			if (ReadSimRam16(address + i * 2) != pattern[i]) {
				match = false;
				break;
			}
		}
		if (match) {
			fprintf(stderr, " %06X", address);
			hits++;
			if (hits == 16) break;
		}
	}
	if (!hits) fprintf(stderr, " not found");
	fprintf(stderr, "\n");
}

static void DumpLisaRomState()
{
	uint32_t ldbase = ReadSimRam32(0x21c);
	fprintf(stderr,
		"ROMSTATE status=%08x d7sav=%08x bootdev=%02x bootdata=%02x %02x %02x %02x %02x %02x "
		"maxmem=%08x totmem=%08x screen=%08x ld_fs_block0=%04x ldbase=%08x loaderr680=%08x\n",
		ReadSimRam32(0x180),
		ReadSimRam32(0x1ac),
		ReadSimRamByte(0x1b3),
		ReadSimRamByte(0x1b4),
		ReadSimRamByte(0x1b5),
		ReadSimRamByte(0x1b6),
		ReadSimRamByte(0x1b7),
		ReadSimRamByte(0x1b8),
		ReadSimRamByte(0x1b9),
		ReadSimRam32(0x294),
		ReadSimRam32(0x2a8),
		ReadSimRam32(0x110),
		ReadSimRam16(0x210),
		ldbase,
		ReadSimRam32(0x680));
	fprintf(stderr,
		"CPUREGS D0=%08x D1=%08x D2=%08x D3=%08x D4=%08x D5=%08x D6=%08x D7=%08x "
		"A0=%08x A1=%08x A2=%08x A3=%08x A4=%08x A5=%08x A6=%08x A7=%08x "
		"USP=%08x SSP=%08x S=%d\n",
		GetCpuReg(0), GetCpuReg(1), GetCpuReg(2), GetCpuReg(3),
		GetCpuReg(4), GetCpuReg(5), GetCpuReg(6), GetCpuReg(7),
		GetCpuReg(8), GetCpuReg(9), GetCpuReg(10), GetCpuReg(11),
		GetCpuReg(12), GetCpuReg(13), GetCpuReg(14), GetCpuA7(),
		GetCpuReg(15), GetCpuReg(16),
		VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT__M68K__DOT__pswS);
	DumpSimRamRange("ROM exception area", 0x280, 0x20);
	DumpSimRamRange("ROM boot data", 0x1b0, 0x30);
	DumpSimRamRange("LDPROF scratch status", 0x800, 0x230);
	if (ldbase < 0x200000) {
		DumpSimRamRange("relocated loader head", ldbase, 0x40);
		DumpSimRamRange("relocated loader descriptor", ldbase + 0x200, 0x40);
	}
}

static bool CpuAtStartupFromMenu()
{
	uint32_t pc = GetCpuPc();
	return pc >= 0xFE2DC0 && pc <= 0xFE2DDF;
}

static bool CpuAtInitialKeyboardScan()
{
	uint32_t pc = GetCpuPc();
	return pc >= 0xFE11C0 && pc <= 0xFE1258;
}

static void DriveHeadlessProfileBoot()
{
	static const uint8_t startup_menu_keys[] = {
		0xF2, // main-row 3 down: any non-Caps-Lock key asks ROM for STARTUP FROM menu
		0x72, // main-row 3 up
	};
	static const uint8_t boot_keys[] = {
		0xFF, // Apple down
		0xF2, // main-row 3 down
		0x72, // main-row 3 up
		0x7F, // Apple up
	};

	if (!headless_boot_profile_started) {
		if (!headless_startup_menu_request_started && CpuAtInitialKeyboardScan() && !CpuAtStartupFromMenu()) {
			headless_startup_menu_request_started = true;
			fprintf(stderr, "headless: requesting STARTUP FROM menu at main_time=%llu pc=%06X\n",
				(unsigned long long)main_time, GetCpuPc());
		}
		if (headless_startup_menu_request_started &&
		    headless_startup_menu_request_step < sizeof(startup_menu_keys) &&
		    VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__sim_cop_key_inject == 0) {
			uint8_t key = startup_menu_keys[headless_startup_menu_request_step++];
			VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__sim_cop_key_inject = key;
			fprintf(stderr, "headless: queued startup-menu key 0x%02X at main_time=%llu pc=%06X\n",
				key, (unsigned long long)main_time, GetCpuPc());
			return;
		}
		if (main_time <= 100000000) {
			return;
		}
		if (headless_boot_profile_ready_time == 0) {
			if (!CpuAtStartupFromMenu()) {
				return;
			}
			headless_boot_profile_ready_time = main_time + 50000000;
			fprintf(stderr, "headless: saw STARTUP FROM menu at main_time=%llu pc=%06X; waiting to inject\n",
				(unsigned long long)main_time, GetCpuPc());
			return;
		}
		if (main_time < headless_boot_profile_ready_time || !CpuAtStartupFromMenu()) {
			return;
		}
		headless_boot_profile_started = true;
		fprintf(stderr, "headless: starting ProFile boot key sequence at main_time=%llu pc=%06X\n",
			(unsigned long long)main_time, GetCpuPc());
	}

	if (headless_boot_profile_step >= sizeof(boot_keys)) {
		return;
	}

	if (VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__sim_cop_key_inject == 0) {
		uint8_t key = boot_keys[headless_boot_profile_step++];
		VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__sim_cop_key_inject = key;
		fprintf(stderr, "headless: queued COP key 0x%02X at main_time=%llu pc=%06X\n",
			key, (unsigned long long)main_time, GetCpuPc());
	}
}

static void PrintHeadlessStatus()
{
	fprintf(stderr,
		"main_time=%llu pc=%06X ON=%d reset=%d pwrsw_n=%d "
		"RESETn=%d BERRn=%d BUSTn=%d HDERn=%d SFERn=%d CDACKn=%d "
		"RSTSWint=%d ONprev=%d "
		"SPIO=%d IOCY=%d MMUIO=%d CPUC1=%d MCY=%d UA=%06X D7=%08x "
		"POL{addr=%06x val=%04x cnt=%02x} "
		"COP{so=%02x ack=%02x kbdin=%02x in=%02x out=%02x kc=%02x,%02x,%02x,%02x dq=%d ra=%d idx=%d "
		"por=%d pc=%03x op=%02x ce=%d pwr=%d den=%x div=%02x icyc=%d res=%d} "
		"KBD{prb=%02x ddrb=%02x pcr=%02x acr=%02x ifr=%02x ier=%02x irq=%d pres=%d} "
		"PP{prb=%02x ddrb=%02x penfall=%04x cmduedge=%04x cmdinen=%x} "
		"PRO{state=%02x max=%02x cmd=%02x strb=%02x rdack=%02x cinrst=%x rst=%d pres=%d blk=%06x c0=%02x stat0=%08x hdr0=%016llx last=%016llx:%016llx} "
		"sd_rd=%03x sd_wr=%03x lba0=%u mounted=%03x "
		"FDC{pc=%04x psm=%02x} "
		"FLP{trk=%02x regs=%04x raddr=%x ldst=%x need=%d sdrd1=%d lba1=%u encst=%x sec=%x cnt=%u flux=%d rda=%d} "
		"BLK{cur=%d r=%d w=%d delay=%d byte=%d ack=%03x}\n",
		(unsigned long long)main_time,
		GetCpuPc(),
		top->ON,
		top->reset,
		top->pwrsw_n_out,
		VERTOPINTERN->emu__DOT__core__DOT___RESET,
		VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT___BERR,
		VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT___BUST,
		VERTOPINTERN->emu__DOT__core__DOT___HDER,
		VERTOPINTERN->emu__DOT__core__DOT___SFER,
		VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT___CDACK,
		VERTOPINTERN->emu__DOT__core__DOT___RSTSW_int,
		VERTOPINTERN->emu__DOT__core__DOT__ON_prev,
		VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT___SPIO,
		VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT___IOCY,
		VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT___MMUIO,
		VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT__CPUC1,
		VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT__MCY,
		VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT__UA,
		GetCpuD7(),
		VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT__dbg_data_addr << 1,
		VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT__dbg_data_val,
		VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT__dbg_data_rd_cnt,
		VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__dbg_so_cnt,
		VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__dbg_ack_cnt,
		VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__dbg_kbdin_cnt,
		VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__dbg_l_in_last,
		VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__dbg_l_out_last,
		VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__kc0,
		VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__kc1,
		VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__kc2,
		VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__kc3,
		VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__DATA_QUEUED_COP,
		VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__READ_ACK_COP,
		VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__sim_cop_byte_idx,
		VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__cop421__DOT__por_n_s,
		VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__cop421__DOT__pm_addr_s,
		VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__cop421__DOT__pm_data_s,
		VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__COPCK_core_enable,
		VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT___PWRSW_COP,
		VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__dummy_COP_D_en,
		VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__cop421__DOT__core_b__DOT__clkgen_b__DOT__n3256,
		(VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__COPCK_core_enable &&
		 VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__cop421__DOT__core_b__DOT__clkgen_b__DOT__n3256 == 0),
		VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__cop421__DOT__core_b__DOT__reset_b__DOT__n3302,
		VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__kbd_via__DOT__prb,
		VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__kbd_via__DOT__ddrb,
		VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__kbd_via__DOT__pcr,
		VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__kbd_via__DOT__acr,
		VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__kbd_via__DOT__irq_flags,
		VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__kbd_via__DOT__irq_mask,
		VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__KBIR,
		VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT___PRES,
		VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__pp_via__DOT__prb,
		VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__pp_via__DOT__ddrb,
		VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__dbg_pen_fall_cnt,
		VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__dbg_cmdu_edge_cnt,
		VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__dbg_cmd_while_en,
		VERTOPINTERN->emu__DOT__profile_i__DOT__state,
		VERTOPINTERN->emu__DOT__profile_i__DOT__max_state,
		VERTOPINTERN->emu__DOT__profile_i__DOT__cmd_edges,
		VERTOPINTERN->emu__DOT__profile_i__DOT__strb_edges,
		VERTOPINTERN->emu__DOT__profile_i__DOT__rd_acks,
		VERTOPINTERN->emu__DOT__profile_i__DOT__cmd_in_rst,
		VERTOPINTERN->emu__DOT__profile_i__DOT__rst_at_cmd,
		VERTOPINTERN->emu__DOT__profile_i__DOT__pres_at_cmd,
		VERTOPINTERN->emu__DOT__profile_i__DOT__block_num,
		VERTOPINTERN->emu__DOT__profile_i__DOT__commandBuffer[0],
		VERTOPINTERN->emu__DOT__profile_i__DOT__dbg_block0_status,
		(unsigned long long)VERTOPINTERN->emu__DOT__profile_i__DOT__dbg_block0_hdr,
		(unsigned long long)VERTOPINTERN->emu__DOT__profile_i__DOT__dbg_last_read_hdr0,
		(unsigned long long)VERTOPINTERN->emu__DOT__profile_i__DOT__dbg_last_read_hdr1,
		top->sd_rd,
		top->sd_wr,
		top->sd_lba[0],
		top->img_mounted,
		VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__FDC_6504__DOT__PC,
		VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__PSM_out,
		VERTOPINTERN->emu__DOT__sony_i__DOT__driveTrack,
		VERTOPINTERN->emu__DOT__sony_i__DOT__driveRegs,
		VERTOPINTERN->emu__DOT__sony_i__DOT__raddr,
		VERTOPINTERN->emu__DOT__sony_i__DOT__ld_state,
		VERTOPINTERN->emu__DOT__sony_i__DOT__need_load,
		VERTOPINTERN->emu__DOT__flp_sd_rd,
		top->sd_lba[1],
		VERTOPINTERN->emu__DOT__sony_i__DOT__enc__DOT__state,
		VERTOPINTERN->emu__DOT__sony_i__DOT__enc__DOT__sector,
		VERTOPINTERN->emu__DOT__sony_i__DOT__enc__DOT__count,
		VERTOPINTERN->emu__DOT__sony_i__DOT__flux,
		VERTOPINTERN->emu__DOT__flp_rda,
		blockdevice.current_disk,
		blockdevice.reading,
		blockdevice.writing,
		blockdevice.ack_delay,
		blockdevice.bytecnt,
		top->sd_ack);
}

struct CrashTraceEntry {
	uint64_t time;
	uint32_t pc;
	uint32_t ua;
	uint32_t d0;
	uint32_t a7;
	uint16_t opcode;
	uint8_t signals; // reset_n, halted, berr_n, bust_n, addrerr
};

// DEBUG (#10 COP misdecode): trace the byte-level keyboard flow during power-up
// -- what the ADAPTER sends (lisa_keycode when a send starts) vs what the COP
// DELIVERS (L_COP_in). If the adapter only ever emits 0x80,0xBF but the COP
// delivers 0x85,0x87,0x80,0xBF, the COP is misdecoding; if the adapter itself
// emits 0x85,0x87, the fault is upstream.
static void TraceCopKbd()
{
	if (main_time > 100000000ULL) return;
	static uint8_t p_kstate = 0xff, p_lin = 0xff;
	static uint32_t p_wr = 0xffffffff;
	uint8_t kstate = VERTOPINTERN->emu__DOT__kbd_adapter_i__DOT__kbd_state;
	uint8_t lkc    = VERTOPINTERN->emu__DOT__kbd_adapter_i__DOT__lisa_keycode;
	uint8_t lin    = VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__dbg_l_in_last;
	uint32_t wr    = VERTOPINTERN->emu__DOT__kbd_adapter_i__DOT__wr_ptr;
	uint32_t rd    = VERTOPINTERN->emu__DOT__kbd_adapter_i__DOT__rd_ptr;
	// kbd_state 3 == SEND_START_BIT in the enum (IDLE=0,WAIT_FOR_HIGH=1,
	// WAIT_TO_SEND=2,SEND_START_BIT=3,...): log the byte at the start of a send.
	if (kstate != p_kstate) {
		if (kstate == 3)
			fprintf(stderr, "KBD t=%llu ADAPTER send byte=0x%02x (rd_ptr=%u wr_ptr=%u)\n",
				(unsigned long long)main_time, lkc, rd, wr);
		p_kstate = kstate;
	}
	if (wr != p_wr) {
		fprintf(stderr, "KBD t=%llu FIFO push -> wr_ptr=%u (rd_ptr=%u)\n",
			(unsigned long long)main_time, wr, rd);
		p_wr = wr;
	}
	if (lin != p_lin) {
		fprintf(stderr, "COP t=%llu delivered L_COP_in=0x%02x mux_sel=%u data_out=%u cop_pc=0x%03x\n",
			(unsigned long long)main_time, lin,
			VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__KBD_mouse_mux_sel,
			VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__KBD_mouse_data_out,
			VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__cop421__DOT__pm_addr_s);
		p_lin = lin;
	}
	// Also sample the mux scan the COP performs (which 2-bit lanes it reads),
	// once per ~2M main_time in the pre-delivery window, to see if it reads
	// garbage mouse/keyboard bits before it emits the first code.
	static uint64_t p_sample = 0;
	if (main_time >= 60000000ULL && main_time - p_sample >= 500000ULL) {
		p_sample = main_time;
		fprintf(stderr, "SCAN t=%llu mux_sel=%u data_out=%u cop_pc=0x%03x lin=0x%02x\n",
			(unsigned long long)main_time,
			VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__KBD_mouse_mux_sel,
			VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__KBD_mouse_data_out,
			VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT__cop421__DOT__pm_addr_s,
			lin);
	}
}

static bool ObserveCrashTrace()
{
	static constexpr size_t trace_size = 256;
	static CrashTraceEntry entries[trace_size];
	static size_t next = 0;
	static size_t count = 0;
	static uint32_t last_pc = 0xffffffff;
	static bool initialized = false;
	static bool loaded_code_seen = false;
	static bool prev_reset_n = true;
	static bool prev_halted = false;
	static bool prev_addrerr = false;
	static bool prev_hpir_n = true;
	static unsigned addrerr_count = 0;
	static bool mmu_utility_installed = false;

	if (!crash_trace) return false;

	uint32_t pc = GetCpuPc();
	bool reset_n = VERTOPINTERN->emu__DOT__core__DOT___RESET;
	bool halted = VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT__M68K__DOT__oHalted;
	bool berr_n = VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT___BERR;
	bool bust_n = VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT___BUST;
	bool addrerr = VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT__M68K__DOT__busAddrErr;
	bool hpir_n = VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT___HPIR;

	if (!initialized) {
		prev_reset_n = reset_n;
		prev_halted = halted;
		prev_addrerr = addrerr;
		prev_hpir_n = hpir_n;
		initialized = true;
	}

	if (pc != last_pc) {
		last_pc = pc;
		entries[next] = {
			main_time,
			pc,
			(uint32_t)VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT__UA,
			GetCpuReg(0),
			GetCpuA7(),
			(uint16_t)top->rootp->emu__DOT__core__DOT__cpu_board__DOT__M68K__DOT__Ir,
			(uint8_t)((reset_n ? 1 : 0) | (halted ? 2 : 0) | (berr_n ? 4 : 0) |
			          (bust_n ? 8 : 0) | (addrerr ? 16 : 0))
		};
		next = (next + 1) % trace_size;
		if (count < trace_size) count++;
	}

	// ROM is at FE0000-FFFFFF. Once the selected ProFile loader executes from
	// RAM, arm the crash triggers and ignore all earlier diagnostic reset edges.
	if (!loaded_code_seen && headless_boot_profile_started &&
	    pc >= 0x000800 && pc < 0xFE0000) {
		loaded_code_seen = true;
		fprintf(stderr, "crash-trace: armed at main_time=%llu pc=%08X\n",
			(unsigned long long)main_time, pc);
	}

	const char* reason = nullptr;
	if (loaded_code_seen) {
		uint16_t mmu_utility_opcode = ReadSimRam16(0x000800);
		if (!mmu_utility_installed && mmu_utility_opcode == 0x2a38) {
			mmu_utility_installed = true;
			fprintf(stderr,
				"crash-trace: MMU utility installed at physical 000800 at main_time=%llu pc=%08X\n",
				(unsigned long long)main_time, pc);
		} else if (mmu_utility_installed && mmu_utility_opcode != 0x2a38) {
			reason = "MMU utility at physical 000800 was overwritten";
		} else if (!mmu_utility_installed && ReadSimRam16(0x001600) == 0x2a38) {
			reason = "MMU utility was copied to physical 001600 instead of 000800";
		}
		if (addrerr && !prev_addrerr) {
			addrerr_count++;
			if (addrerr_count <= 8) {
				fprintf(stderr,
					"crash-trace: address-error marker %u at main_time=%llu pc=%08X a7=%08X\n",
					addrerr_count, (unsigned long long)main_time, pc, GetCpuA7());
			} else if (addrerr_count == 9) {
				fprintf(stderr, "crash-trace: suppressing further address-error markers\n");
			}
		}
		if (!reason) {
			if (pc >= 0x00A84000 && pc < 0x00A84200 &&
			    ReadSimRam16(0x000800) != 0x2a38) {
				reason = "entered MMU utility but its physical code page is invalid";
			} else if (headless_stop_pc != 0xffffffff &&
			           main_time >= headless_stop_start && pc == headless_stop_pc) {
				reason = "requested PC trace stop";
			} else if (!hpir_n && prev_hpir_n && mmu_utility_installed) {
				reason = "level-7/high-priority interrupt asserted";
			} else if (halted && !prev_halted) reason = "fx68k HALT/double fault";
			else if (!reset_n && prev_reset_n) reason = "CPU-board reset";
		}
	}

	prev_reset_n = reset_n;
	prev_halted = halted;
	prev_addrerr = addrerr;
	prev_hpir_n = hpir_n;
	if (!reason) return false;

	fprintf(stderr, "\ncrash-trace: %s at main_time=%llu pc=%08X\n",
		reason, (unsigned long long)main_time, pc);
	fprintf(stderr,
		"crash-trace: HPIRn=%d NMIsync=%d HDERlat_sync=%d SFERlat_sync=%d "
		"HDERlat=%d SFERlat=%d NMICOP=%d\n",
		hpir_n,
		VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT___NMI_sync,
		VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT___HDER_latched_sync,
		VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT___SFER_latched_sync,
		VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT___HDER_latched,
		VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT___SFER_latched,
		VERTOPINTERN->emu__DOT__core__DOT__io_board__DOT___NMI_COP);
	fprintf(stderr, "crash-trace: last %zu distinct instruction PCs:\n", count);
	for (size_t i = 0; i < count; i++) {
		const CrashTraceEntry& entry = entries[(next + trace_size - count + i) % trace_size];
		fprintf(stderr,
			"  t=%llu pc=%08X op=%04X ua=%06X d0=%08X a7=%08X "
			"R=%d H=%d B=%d T=%d A=%d  %s\n",
			(unsigned long long)entry.time, entry.pc, entry.opcode, entry.ua,
			entry.d0, entry.a7,
			(entry.signals & 1) != 0, (entry.signals & 2) != 0,
			(entry.signals & 4) != 0, (entry.signals & 8) != 0,
			(entry.signals & 16) != 0,
			disassemble_68k(entry.pc, entry.opcode));
	}
	PrintHeadlessStatus();
	fprintf(stderr,
		"RAMADDR cpu_A=%05X physical=%06X latched=%02X:%X adder=%03X TD=%03X "
		"MALEn=%d buffered_RA=%02X row=%02X col=%02X sram_word=%05X decoded=%06X\n",
		VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT__A,
		VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT__A << 1,
		// latched_MMU_address is a single 12-bit field for RTL bits [20:9];
		// [20:13] = field bits [11:4], [12:9] = field bits [3:0].
		((VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT__latched_MMU_address >> 4) & 0xFF),
		(VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT__latched_MMU_address & 0xF),
		VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT__MMU_adder_out,
		VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT__TD,
		VERTOPINTERN->emu__DOT__core__DOT__cpu_board__DOT___MALE,
		VERTOPINTERN->emu__DOT__core__DOT__slot1__DOT__buffered_RA,
		VERTOPINTERN->emu__DOT__core__DOT__slot1__DOT__SDRAM_2MB__DOT__row_addr,
		VERTOPINTERN->emu__DOT__core__DOT__slot1__DOT__SDRAM_2MB__DOT__col_addr,
		VERTOPINTERN->emu__DOT__core__DOT__slot1__DOT__SDRAM_2MB__DOT__sram_word_addr,
		SimRamIndexToPhysicalByte(VERTOPINTERN->emu__DOT__core__DOT__slot1__DOT__SDRAM_2MB__DOT__sram_word_addr));
	uint32_t ram_word = VERTOPINTERN->emu__DOT__core__DOT__slot1__DOT__SDRAM_2MB__DOT__sram_word_addr;
	auto& ram = VERTOPINTERN->emu__DOT__core__DOT__slot1__DOT__SDRAM_2MB__DOT__sim_ram;
	fprintf(stderr, "crash-trace: last SRAM word=%05X (physical byte=%06X) q=%04X contents:",
		ram_word, SimRamIndexToPhysicalByte(ram_word),
		VERTOPINTERN->emu__DOT__core__DOT__slot1__DOT__SDRAM_2MB__DOT__sim_ram_q);
	uint32_t first_word = ram_word >= 8 ? ram_word - 8 : 0;
	for (uint32_t word = first_word; word < first_word + 24; word++) {
		if (((word - first_word) % 8) == 0) fprintf(stderr, "\n  %05X:", word);
		fprintf(stderr, " %04X", ram[word]);
	}
	fprintf(stderr, "\n");
	DumpMmuSegment(0x54, 0, 0xA84000);
	uint32_t frame_base = GetCpuReg(14) - 0x20;
	uint8_t frame_segment = frame_base >> 17;
	for (unsigned context = 0; context < 4; context++) {
		DumpMmuSegment(frame_segment, context, frame_base);
	}
	static const uint16_t mmu_utility_pattern[] = {0x2a38, 0x02a4, 0xe08d, 0xe28d};
	static const uint16_t bad_fetch_pattern[] = {0x8500, 0x252a};
	FindSimRamPattern("MMU utility signature in physical RAM", mmu_utility_pattern,
		sizeof(mmu_utility_pattern) / sizeof(mmu_utility_pattern[0]));
	FindSimRamPattern("bad trap-fetch signature in physical RAM", bad_fetch_pattern,
		sizeof(bad_fetch_pattern) / sizeof(bad_fetch_pattern[0]));
	DumpLisaRomState();
	return true;
}

int verilate() {
	if (!Verilated::gotFinish()) {
		if (soft_reset) {
			fprintf(stderr, "soft_reset.. in gotFinish\n");
			top->soft_reset = 1;
			soft_reset = 0;
			soft_reset_time = 0;
			fprintf(stderr, "turning on %x\n", top->soft_reset);
		}
		if (clk_sys.IsRising()) {
			soft_reset_time++;
		}
		if (soft_reset_time == initialReset) {
			top->soft_reset = 0;
			fprintf(stderr, "turning off %x\n", top->soft_reset);
			fprintf(stderr, "soft_reset_time %ld initialReset %x\n", soft_reset_time, initialReset);
		}

		// Assert reset during startup
		if (main_time < initialReset) { top->reset = 1; }
		// Deassert reset after startup
		if (main_time == initialReset) { top->reset = 0; }

		// Clock dividers
		clk_sys.Tick();

		// Set system clock in core
		top->clk_sys = clk_sys.clk;
		top->adam = adam_mode;
		g_vbl_count = headless_mode ? 0 : video.count_frame;

		// Simulate both edges of system clock
		if (clk_sys.clk != clk_sys.old) {
			if (clk_sys.IsRising() && *bus.ioctl_download != 1) blockdevice.BeforeEval(main_time);
			if (clk_sys.clk) {
				input.BeforeEval();
				bus.BeforeEval();
			}
			top->eval();
			TraceCopKbd();
			if (!MaybePatchFullRamTest()) {
				headless_stop_requested = true;
			}
			if (ObserveCrashTrace()) {
				headless_stop_requested = true;
			}
			if (headless_mode && headless_stop_pc != 0xffffffff &&
			    main_time >= headless_stop_start && GetCpuPc() == headless_stop_pc) {
				fprintf(stderr, "headless: stop-pc hit at main_time=%llu pc=%06X\n",
					(unsigned long long)main_time, GetCpuPc());
				PrintHeadlessStatus();
				headless_stop_requested = true;
			}

			// Disassembly output
			if (cpu_trace_enable) {
				static uint32_t last_pc = 0xFFFFFFFF;
				uint32_t pc = GetCpuPc();
				if (pc != last_pc) {
					last_pc = pc;
					uint16_t opcode = top->rootp->emu__DOT__core__DOT__cpu_board__DOT__M68K__DOT__Ir;
					const char* disasm = disassemble_68k(pc, opcode);
					fprintf(stderr, "[F%d] %06X: %04X  %s\n", headless_mode ? 0 : video.count_frame, pc, opcode, disasm);
					if (trace_console_cnt++ < 1000) {
						console.AddLog("[F%d] %06X: %04X  %s", headless_mode ? 0 : video.count_frame, pc, opcode, disasm);
					} else if (trace_console_cnt == 1000) {
						console.AddLog("... Trace console output rate-limited to first 1000 instructions. Check stderr/task logs for full trace.");
					}
				}
			}


			// Log 6502 instructions (disabled for Lisa)

			if (clk_sys.clk) { bus.AfterEval(); blockdevice.AfterEval(); }
		}

#ifndef DISABLE_AUDIO
		if (clk_sys.IsRising())
		{
			audio.Clock(top->AUDIO_L, top->AUDIO_R);
		}
#endif

		// Output pixels on rising edge of pixel clock
		if ((!headless_mode || !headless_screenshot_path.empty()) && clk_sys.IsRising() && top->CE_PIXEL) {
			uint32_t colour = 0xFF000000 | top->VGA_B << 16 | top->VGA_G << 8 | top->VGA_R;
			video.Clock(top->VGA_HB, top->VGA_VB, top->VGA_HS, top->VGA_VS, colour);
		}

		if (clk_sys.IsRising()) {
			if (headless_mode && headless_boot_profile) {
				DriveHeadlessProfileBoot();
			}

			// IWM EMULATION HERE
			//         CData/*7:0*/ emu__DOT__top__DOT__core__DOT__iwm__DOT__addr;
    //    CData/*0:0*/ emu__DOT__top__DOT__core__DOT__iwm__DOT__rw;
     //   CData/*7:0*/ emu__DOT__top__DOT__core__DOT__iwm__DOT__din;
      //  CData/*7:0*/ emu__DOT__top__DOT__core__DOT__iwm__DOT__dout;
       // CData/*0:0*/ emu__DOT__top__DOT__core__DOT__iwm__DOT__irq;
     //   CData/*0:0*/ emu__DOT__top__DOT__core__DOT__iwm__DOT__strobe;
     //   CData/*7:0*/ emu__DOT__top__DOT__core__DOT__iwm__DOT__DISK35;

			// IWM EMULATION HERE (disabled for Lisa)

			if (headless_mode && headless_status_interval != 0 &&
			    main_time >= headless_status_start &&
			    (main_time % headless_status_interval == 0)) {
				PrintHeadlessStatus();
				fflush(stderr);
			} else if (!headless_mode && main_time % 5000000 == 0) {
				fprintf(stderr, "main_time: %lld, ON: %d, reset: %d, frame: %d, pwrsw_n: %d\n",
					(long long)main_time, top->ON, top->reset, video.count_frame, top->pwrsw_n_out);
				fflush(stderr);
			}

			main_time++;
		}
		return 1;
	}

	// Stop verilating and cleanup
	top->final();
	delete top;
	exit(0);
	return 0;
}

void RunBatch(int steps)
{
	for (int step = 0; step < steps; step++) {
		verilate();
		if (break_pending) {
			run_state = RunState::Stopped;
			break_pending = false;
			break;
		}
	}
}

void RunHeadless(uint64_t max_cycles)
{
	top->menu = 0;
	top->joystick_0 = 0;
	top->joystick_1 = 0;
	top->ps2_key = 0;
	top->ps2_mouse = 0;
	top->ps2_mouse_ext = 0;

	const int batch = 100000;
	while (max_cycles == 0 || main_time < max_cycles) {
		for (int i = 0; i < batch && (max_cycles == 0 || main_time < max_cycles); i++) {
			verilate();
			if (headless_stop_requested) {
				break;
			}
		}
		if (headless_stop_requested) {
			break;
		}
	}
	if (headless_mode) {
		PrintHeadlessStatus();
		if (headless_dump_rom_state) {
			DumpLisaRomState();
		}
	}
	fprintf(stderr, "headless complete: main_time=%llu ON=%d reset=%d pwrsw_n=%d\n",
		(unsigned long long)main_time, top->ON, top->reset, top->pwrsw_n_out);
	if (!headless_screenshot_path.empty()) {
		if (video.SavePPM(headless_screenshot_path.c_str())) {
			fprintf(stderr, "headless: wrote screenshot %s frame=%d\n",
				headless_screenshot_path.c_str(), video.count_frame);
		} else {
			fprintf(stderr, "headless: failed to write screenshot %s\n",
				headless_screenshot_path.c_str());
		}
	}
	top->final();
	delete top;
	top = NULL;
}

unsigned char mouse_clock = 0;
unsigned char mouse_clock_reduce = 0;
unsigned char mouse_buttons = 0;
unsigned char mouse_x = 0;
unsigned char mouse_y = 0;

char spinner_toggle = 0;

int main(int argc, char** argv, char** env) {
	SimOptions options;
	if (!ParseOptions(argc, argv, &options)) {
		return 1;
	}
	headless_mode = options.headless;
	headless_status_interval = options.status_interval;
	headless_status_start = options.status_start;
	headless_stop_pc = options.stop_pc;
	headless_stop_start = options.stop_start;
	headless_boot_profile = options.boot_profile;
	skip_ram_test = options.skip_ram_test;
	crash_trace = options.crash_trace;
	headless_screenshot_path = options.screenshot;
	headless_dump_rom_state = options.dump_rom_state;
	cpu_trace_enable = options.trace;

	// Create core and initialise
	top = new Vemu();
	Verilated::commandArgs(argc, argv);



#ifdef WIN32
	// Attach debug console to the verilated code
	Verilated::setDebug(console);
#endif


	// Load debug trace
	std::string line;
	std::ifstream fin(tracefilename);
	while (getline(fin, line)) {
		log_mame.push_back(line);
	}
	//a2_name_count = size(a2_stuff);
	a2_name_count = sizeof(a2_stuff)/sizeof(a2_stuff[0]);

	// Attach bus
	bus.ioctl_addr = &top->ioctl_addr;
	bus.ioctl_index = &top->ioctl_index;
	bus.ioctl_wait = &top->ioctl_wait;
	bus.ioctl_download = &top->ioctl_download;
	//bus.ioctl_upload = &top->ioctl_upload;
	bus.ioctl_wr = &top->ioctl_wr;
	bus.ioctl_dout = &top->ioctl_dout;
	//bus.ioctl_din = &top->ioctl_din;
	input.ps2_key = &top->ps2_key;

	// hookup blk device
	blockdevice.sd_lba[0] = &top->sd_lba[0];
	blockdevice.sd_lba[1] = &top->sd_lba[1];
	blockdevice.sd_rd = &top->sd_rd;
	blockdevice.sd_wr = &top->sd_wr;
	blockdevice.sd_ack = &top->sd_ack;
	blockdevice.sd_buff_addr = &top->sd_buff_addr;
	blockdevice.sd_buff_dout = &top->sd_buff_dout;
	blockdevice.sd_buff_din[0] = &top->sd_buff_din[0];
	blockdevice.sd_buff_din[1] = &top->sd_buff_din[1];
	blockdevice.sd_buff_wr = &top->sd_buff_wr;
	blockdevice.img_mounted = &top->img_mounted;
	blockdevice.img_readonly = &top->img_readonly;
	blockdevice.img_size = &top->img_size;

	blockdevice.MountDisk(options.profile_image, 0);

	// Sony 400K floppy on slot 1 (DiskCopy 4.2 image). Optional.
	if (!options.floppy_image.empty()) {
		blockdevice.MountDisk(options.floppy_image, 1);
	}

	if (options.headless) {
		if (!headless_screenshot_path.empty() && video.InitialiseHeadless() != 0) {
			fprintf(stderr, "headless: failed to initialise screenshot framebuffer\n");
			return 1;
		}
		RunHeadless(options.cycles);
		return 0;
	}

#ifndef DISABLE_AUDIO
	audio.Initialise();
#endif

	// Set up input module
	input.Initialise();
#ifdef WIN32
	input.SetMapping(input_up, DIK_UP);
	input.SetMapping(input_right, DIK_RIGHT);
	input.SetMapping(input_down, DIK_DOWN);
	input.SetMapping(input_left, DIK_LEFT);
	input.SetMapping(input_a, DIK_Z); // A
	input.SetMapping(input_b, DIK_X); // B
	input.SetMapping(input_x, DIK_A); // X
	input.SetMapping(input_y, DIK_S); // Y
	input.SetMapping(input_l, DIK_Q); // L
	input.SetMapping(input_r, DIK_W); // R
	input.SetMapping(input_select, DIK_1); // Select
	input.SetMapping(input_start, DIK_2); // Start
	input.SetMapping(input_menu, DIK_M); // System menu trigger

#else
	input.SetMapping(input_up, SDL_SCANCODE_UP);
	input.SetMapping(input_right, SDL_SCANCODE_RIGHT);
	input.SetMapping(input_down, SDL_SCANCODE_DOWN);
	input.SetMapping(input_left, SDL_SCANCODE_LEFT);
	input.SetMapping(input_a, SDL_SCANCODE_A);
	input.SetMapping(input_b, SDL_SCANCODE_B);
	input.SetMapping(input_x, SDL_SCANCODE_X);
	input.SetMapping(input_y, SDL_SCANCODE_Y);
	input.SetMapping(input_l, SDL_SCANCODE_L);
	input.SetMapping(input_r, SDL_SCANCODE_E);
	input.SetMapping(input_start, SDL_SCANCODE_1);
	input.SetMapping(input_select, SDL_SCANCODE_2);
	input.SetMapping(input_menu, SDL_SCANCODE_M);
#endif
	// Setup video output
	if (video.Initialise(windowTitle) == 1) { return 1; }

	// iwm_load_disk();
	//bus.QueueDownload("floppy.nib",1,0);
//blockdevice.MountDisk("floppy.nib",0);

       // iwm_init();
       // iwm_reset();

#ifdef WIN32
	MSG msg;
	ZeroMemory(&msg, sizeof(msg));
	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			continue;
		}
#else
	bool done = false;
	while (!done)
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			ImGui_ImplSDL2_ProcessEvent(&event);
			if (event.type == SDL_QUIT)
				done = true;
		}
#endif
		video.StartFrame();

		{
			static int render_cnt = 0;
			if (render_cnt++ % 60 == 0) {
				fprintf(stderr, "DEBUG: GUI Frame %d, main_time: %lld, ON: %d, reset: %d, pwrsw_n: %d, run_state: %d\n",
					video.count_frame, (long long)main_time, top->ON, top->reset, top->pwrsw_n_out, (int)run_state);
				fflush(stderr);
			}
		}

		input.Read();


		// Draw GUI
		// --------
		ImGui::NewFrame();

		// Simulation control window
		ImGui::Begin(windowTitle_Control);
		ImGui::SetWindowPos(windowTitle_Control, ImVec2(0, 0), ImGuiCond_Once);
		ImGui::SetWindowSize(windowTitle_Control, ImVec2(500, 150), ImGuiCond_Once);
		if (ImGui::Button("Reset simulation")) { resetSim(); } ImGui::SameLine();
		ImGui::Checkbox("STOPONDIFF", &stop_on_log_mismatch); ImGui::SameLine();
		ImGui::Checkbox("CPU Trace", &cpu_trace_enable);
		if (ImGui::Button("Start running")) { run_state = RunState::Running; } ImGui::SameLine();
		if (ImGui::Button("Stop running")) { run_state = RunState::Stopped; } ImGui::SameLine();
		ImGui::PushItemWidth(100);
		ImGui::InputInt("Run batch size", &batchSize, 1000, 10000);
		ImGui::PopItemWidth();
		if (run_state == RunState::SingleClock || run_state == RunState::MultiClock) { run_state = RunState::Stopped;}
		ImGui::Text("Clock step:"); ImGui::SameLine();
		if (ImGui::Button("Single")) { run_state = RunState::SingleClock; }
		ImGui::SameLine();
		if (ImGui::Button("Multi")) { run_state = RunState::MultiClock; }
		ImGui::SameLine();
		ImGui::PushItemWidth(100);
		ImGui::InputInt("Multi clock amount", &multi_step_amount, 1, 10);
		ImGui::PopItemWidth();
		ImGui::Text("CPU:"); ImGui::SameLine();
		if (ImGui::Button("Step")) { run_state = RunState::StepIn; }
		ImGui::SameLine();
		if (ImGui::Button("Next IRQ")) { run_state = RunState::NextIRQ; }

		//ImGui::SameLine();
		//		if (ImGui::Button("Load ROM"))
			//ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey", "Choose File", ".rom", ".");

				//if (ImGui::Button("Soft Reset")) { fprintf(stderr,"soft reset\n"); soft_reset=1; } ImGui::SameLine();

		ImGui::End();

		// Debug log window
		console.Draw(windowTitle_DebugLog, &showDebugLog, ImVec2(500, 700));
		ImGui::SetWindowPos(windowTitle_DebugLog, ImVec2(0, 160), ImGuiCond_Once);

		// Debug panels disabled for Lisa


		int windowX = 550;
		int windowWidth = (VGA_WIDTH * VGA_SCALE_X) + 24;
		int windowHeight = (VGA_HEIGHT * VGA_SCALE_Y) + 90;

		// Video window
		ImGui::Begin(windowTitle_Video);
		ImGui::SetWindowPos(windowTitle_Video, ImVec2(windowX, 0), ImGuiCond_Once);
		ImGui::SetWindowSize(windowTitle_Video, ImVec2(windowWidth, windowHeight), ImGuiCond_Once);

		ImGui::SliderFloat("Zoom", &vga_scale, 1, 8); ImGui::SameLine();
		ImGui::SliderInt("Rotate", &video.output_rotate, -1, 1); ImGui::SameLine();
		ImGui::Checkbox("Flip V", &video.output_vflip);
		ImGui::Text("main_time: %ld frame_count: %d sim FPS: %f", main_time, video.count_frame, video.stats_fps);
		//ImGui::Text("pixel: %06d line: %03d", video.count_pixel, video.count_line);

		// Draw VGA output
		ImGui::Image(video.texture_id, ImVec2(video.output_width * VGA_SCALE_X, video.output_height * VGA_SCALE_Y));
		ImGui::End();

		if (ImGuiFileDialog::Instance()->Display("ChooseFileDlgKey"))
		{
			// action if OK
			if (ImGuiFileDialog::Instance()->IsOk())
			{
				std::string filePathName = ImGuiFileDialog::Instance()->GetFilePathName();
				std::string filePath = ImGuiFileDialog::Instance()->GetCurrentPath();
				// action
				fprintf(stderr, "filePathName: %s\n", filePathName.c_str());
				fprintf(stderr, "filePath: %s\n", filePath.c_str());
				bus.QueueDownload(filePathName, 1, 1);
			}

			// close
			ImGuiFileDialog::Instance()->Close();
		}


#ifndef DISABLE_AUDIO

		ImGui::Begin(windowTitle_Audio);
		ImGui::SetWindowPos(windowTitle_Audio, ImVec2(windowX, windowHeight), ImGuiCond_Once);
		ImGui::SetWindowSize(windowTitle_Audio, ImVec2(windowWidth, 250), ImGuiCond_Once);


		//float vol_l = ((signed short)(top->AUDIO_L) / 256.0f) / 256.0f;
		//float vol_r = ((signed short)(top->AUDIO_R) / 256.0f) / 256.0f;
		//ImGui::ProgressBar(vol_l + 0.5f, ImVec2(200, 16), 0); ImGui::SameLine();
		//ImGui::ProgressBar(vol_r + 0.5f, ImVec2(200, 16), 0);

		int ticksPerSec = (24000000 / 60);
		if (run_state == RunState::Running) {
			audio.CollectDebug((signed short)top->AUDIO_L, (signed short)top->AUDIO_R);
		}
		int channelWidth = (windowWidth / 2) - 16;
		ImPlot::CreateContext();
		if (ImPlot::BeginPlot("Audio - L", ImVec2(channelWidth, 220), ImPlotFlags_NoLegend | ImPlotFlags_NoMenus | ImPlotFlags_NoTitle)) {
			ImPlot::SetupAxes("T", "A", ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_NoTickMarks, ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_NoTickMarks);
			ImPlot::SetupAxesLimits(0, 1, -1, 1, ImPlotCond_Once);
			ImPlot::PlotStairs("", audio.debug_positions, audio.debug_wave_l, audio.debug_max_samples, audio.debug_pos);
			ImPlot::EndPlot();
		}
		ImGui::SameLine();
		if (ImPlot::BeginPlot("Audio - R", ImVec2(channelWidth, 220), ImPlotFlags_NoLegend | ImPlotFlags_NoMenus | ImPlotFlags_NoTitle)) {
			ImPlot::SetupAxes("T", "A", ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_NoTickMarks, ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_NoTickMarks);
			ImPlot::SetupAxesLimits(0, 1, -1, 1, ImPlotCond_Once);
			ImPlot::PlotStairs("", audio.debug_positions, audio.debug_wave_r, audio.debug_max_samples, audio.debug_pos);
			ImPlot::EndPlot();
		}
		ImPlot::DestroyContext();
		ImGui::End();
#endif

		video.UpdateTexture();


		// Pass inputs to sim

		top->menu = input.inputs[input_menu];

		top->joystick_0 = 0;
		for (int i = 0; i < input.inputCount; i++)
		{
			if (input.inputs[i]) { top->joystick_0 |= (1 << i); }
		}
		top->joystick_1 = top->joystick_0;

		/*top->joystick_analog_0 += 1;
		top->joystick_analog_0 -= 256;*/
		//top->paddle_0 += 1;
		//if (input.inputs[0] || input.inputs[1]) {
		//	spinner_toggle = !spinner_toggle;
		//	top->spinner_0 = (input.inputs[0]) ? 16 : -16;
		//	for (char b = 8; b < 16; b++) {
		//		top->spinner_0 &= ~(1UL << b);
		//	}
		//	if (spinner_toggle) { top->spinner_0 |= 1UL << 8; }
		//}

		mouse_buttons = 0;
		mouse_x = 0;
		mouse_y = 0;
		if (input.inputs[input_left]) { mouse_x = -2; }
		if (input.inputs[input_right]) { mouse_x = 2; }
		if (input.inputs[input_up]) { mouse_y = 2; }
		if (input.inputs[input_down]) { mouse_y = -2; }

		if (input.inputs[input_a]) { mouse_buttons |= (1UL << 0); }
		if (input.inputs[input_b]) { mouse_buttons |= (1UL << 1); }

		unsigned long mouse_temp = mouse_buttons;
		mouse_temp += (mouse_x << 8);
		mouse_temp += (mouse_y << 16);
		if (mouse_clock) { mouse_temp |= (1UL << 24); }
		mouse_clock = !mouse_clock;

		top->ps2_mouse = mouse_temp;
		top->ps2_mouse_ext = mouse_x + (mouse_buttons << 8);

		// Run simulation
		switch (run_state) {
		case RunState::StepIn:
		case RunState::NextIRQ:
		case RunState::Running: RunBatch(batchSize); break;
		case RunState::SingleClock: verilate(); break;
		case RunState::MultiClock: RunBatch(multi_step_amount); break;
		default: std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	}

	// Clean up before exit
	// --------------------

#ifndef DISABLE_AUDIO
	audio.CleanUp();
#endif 
	video.CleanUp();
	input.CleanUp();

	return 0;
}
