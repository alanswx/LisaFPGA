#include <iostream>
#include <queue>
#include <string>

#include "sim_blkdevice.h"
#include "sim_console.h"
#include "verilated.h"

#ifndef _MSC_VER
#else
#define WIN32
#endif


static DebugConsole console;

IData* sd_lba[kVDNUM]= {NULL,NULL,NULL,NULL,NULL,
                   NULL,NULL,NULL,NULL,NULL};
SData* sd_rd=NULL;
SData* sd_wr=NULL;
SData* sd_ack=NULL;
SData* sd_buff_addr=NULL;
SData* sd_buff_dout=NULL;
SData* sd_buff_din[kVDNUM]= {NULL,NULL,NULL,NULL,NULL,
                   NULL,NULL,NULL,NULL,NULL};
CData* sd_buff_wr=NULL;
SData* img_mounted=NULL;
CData* img_readonly=NULL;
QData* img_size=NULL;


#define bitset(byte,nbit)   ((byte) |=  (1<<(nbit)))
#define bitclear(byte,nbit) ((byte) &= ~(1<<(nbit)))
#define bitflip(byte,nbit)  ((byte) ^=  (1<<(nbit)))
#define bitcheck(byte,nbit) ((byte) &   (1<<(nbit)))


void SimBlockDevice::MountDisk( std::string file, int index) {
	disk[index].open(file.c_str(), std::ios::out | std::ios::in | std::ios::binary | std::ios::ate);
        if (disk[index]) {
		fprintf(stderr,"we are here\n");
           // we shouldn't do the actual mount here..
           disk_size[index]= disk[index].tellg();
	//fprintf(stderr,"mount size %ld\n",disk_size[index]);
           disk[index].seekg(0);
           mountQueue[index]=1;
           printf("disk %d inserted (%s)\n",index,file.c_str());
        }else {
		fprintf(stderr,"some kind of error: %s\n",file.c_str());
	}

}


void SimBlockDevice::BeforeEval(uint64_t cycles)
{
    if (cycles < 2000) return;

    *this->sd_buff_wr = 0;

    for (int i = 0; i < kVDNUM; i++) {
        if (!reading && !writing && mountQueue[i] && !*this->img_mounted) {
            fprintf(stderr, "mounting.. %d\n", i);
            mountQueue[i] = 0;
            *this->img_size = disk_size[i];
            *this->img_readonly = 0;
            fprintf(stderr, "img_size .. %llu\n", (unsigned long long)*this->img_size);
            disk[i].seekg(0);
            bitset(*this->img_mounted, i);
            ack_delay = 1200;
            return;
        }

        if (ack_delay > 1 && bitcheck(*this->img_mounted, i)) {
            ack_delay--;
            return;
        }

        if (ack_delay == 1 && bitcheck(*this->img_mounted, i)) {
            fprintf(stderr, "mounting flag cleared  %d\n", i);
            bitclear(*this->img_mounted, i);
            ack_delay = 0;
            return;
        }
    }

    if (current_disk < 0 && !reading && !writing && ack_delay == 0) {
        for (int i = 0; i < kVDNUM; i++) {
            bool want_read = bitcheck(*this->sd_rd, i);
            bool want_write = bitcheck(*this->sd_wr, i);
            if (!want_read && !want_write) continue;

            current_disk = i;
            reading = want_read;
            writing = want_write;
            bytecnt = 0;
            *this->sd_buff_addr = 0;

            int lba = *(this->sd_lba[i]);
            disk[i].clear();
            disk[i].seekg(lba * kBLKSZ);
            ack_delay = 1200;
            break;
        }
    }

    if (current_disk < 0) return;

    // Latency phase: sd_ack LOW while the "HPS" fetches the sector. This mimics
    // the real MiSTer round-trip to the ARM before any data is available.
    if (ack_delay > 0) {
        bitclear(*this->sd_ack, current_disk);
        ack_delay--;
        return;
    }

    // Transfer phase: the REAL MiSTer HPS holds sd_ack HIGH for the ENTIRE
    // sd_buff_wr stream and drops it only when the sector is done. The core
    // captures data while sd_ack==1 and treats the FALLING edge as "complete".
    // (The old model kept sd_ack LOW during the stream and pulsed it HIGH only
    // afterward, which let rising-edge-completion cores pass in sim but fail on
    // hardware — see references/MiSTer_HPS_SD_Protocol_Findings.md.)
    bitset(*this->sd_ack, current_disk);

    if (reading) {
        if (bytecnt < 256) {
            uint8_t low = disk[current_disk].get();
            uint8_t high = disk[current_disk].get();
            *this->sd_buff_dout = (high << 8) | low;
            *this->sd_buff_addr = bytecnt++;
            *this->sd_buff_wr = 1;
            return;                    // sd_ack stays HIGH while streaming
        }
        reading = false;
    } else if (writing) {
        // sd_buff_din carries the core's data for the address we drove on the
        // previous eval (1-cycle read latency on the core's buffer).
        if (*this->sd_buff_addr != bytecnt && *this->sd_buff_addr < 256) {
            uint16_t val = *(this->sd_buff_din[current_disk]);
            disk[current_disk].put(val & 0xFF);
            disk[current_disk].put((val >> 8) & 0xFF);
            *this->sd_buff_addr = bytecnt;
        }
        if (bytecnt < 256) {
            bytecnt++;
            return;                    // sd_ack stays HIGH while streaming
        }
        writing = false;
    }

    // Whole sector transferred: drop sd_ack (its FALLING edge is "done") and go
    // idle so the next sd_rd/sd_wr can start a fresh transfer.
    bitclear(*this->sd_ack, current_disk);
    ack_delay = 0;
    current_disk = -1;
}

void SimBlockDevice::AfterEval()
{
}


SimBlockDevice::SimBlockDevice(DebugConsole c) {
	console = c;
        current_disk=-1;
        bytecnt=0;
        reading=false;
        writing=false;
        ack_delay=0;

        sd_rd = NULL;
        sd_wr = NULL;
        sd_ack = NULL;
        sd_buff_addr = NULL;
        sd_buff_dout = NULL;
	for (int i=0;i<kVDNUM;i++) {
           sd_lba[i] = NULL;
	   sd_buff_din[i] = NULL;
           mountQueue[i]=0;
        }
        sd_buff_wr=NULL;
        img_mounted=NULL;
        img_readonly=NULL;
        img_size=NULL;
}

SimBlockDevice::~SimBlockDevice() {

}
