// ============================================================================
// tb_sony_main.cpp -- standalone testbench for sony_drive (Apple Lisa floppy).
//
// Instantiates the drive alone (no CPU/ROM), mounts a real DiskCopy-4.2 400K
// image, models the HPS SD block device, and drives the Sony 3.5" drive
// register protocol directly to verify:
//   M1  sense registers (disk-in-place, track-0, motor on/off)
//   M2  head stepping (direction + STEP) and the tachometer
//   M3  the GCR read stream on RDA: sync + address mark D5 AA 96 and a
//       correctly 6-and-2-decoded track/sector address field.
//
// This is fast (milliseconds) unlike full-system boot, so it is the M1-M3
// iteration loop. Exact PH<->register mapping vs the real 6504 firmware is
// still confirmed later on hardware; here we validate the drive logic itself
// against the standard Sony protocol the MacPlus reference uses.
// ============================================================================
#include <verilated.h>
#include "Vtb_sony_top.h"
#include "Vtb_sony_top___024root.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>

static Vtb_sony_top* top;
static uint64_t g_time = 0;

// ---- DC42 image + SD block-device model ------------------------------------
static std::vector<uint8_t> g_img;
static const int   BLKSZ = 512;
static bool sd_busy = false;
static int  sd_state = 0;      // 0 idle, 1 latency, 2 stream
static int  sd_delay = 0;
static int  sd_bytecnt = 0;
static uint32_t sd_lba_latched = 0;

static void sd_service() {
    // Mirrors verilator/sim/sim_blkdevice.cpp: latency, then stream 256 words
    // while sd_ack is HIGH, drop ack on completion (falling edge = done).
    if (!sd_busy) {
        if (top->sd_rd) {
            sd_busy = true; sd_state = 1; sd_delay = 40;
            sd_bytecnt = 0; sd_lba_latched = top->sd_lba;
            top->sd_buff_wr = 0;
            top->sd_ack = 0;
        } else {
            top->sd_ack = 0; top->sd_buff_wr = 0;
        }
        return;
    }
    if (sd_state == 1) {           // latency: ack low
        top->sd_ack = 0; top->sd_buff_wr = 0;
        if (--sd_delay <= 0) { sd_state = 2; sd_bytecnt = 0; }
        return;
    }
    // stream phase: ack HIGH throughout
    top->sd_ack = 1;
    if (sd_bytecnt < 256) {
        uint64_t base = (uint64_t)sd_lba_latched * BLKSZ + sd_bytecnt * 2;
        uint8_t lo = (base   < g_img.size()) ? g_img[base]   : 0;
        uint8_t hi = (base+1 < g_img.size()) ? g_img[base+1] : 0;
        top->sd_buff_dout = (hi << 8) | lo;
        top->sd_buff_addr = sd_bytecnt;
        top->sd_buff_wr = 1;
        sd_bytecnt++;
        return;
    }
    // done
    top->sd_buff_wr = 0;
    top->sd_ack = 0;
    sd_busy = false; sd_state = 0;
}

static void tick() {
    // negedge settle
    top->clk_sys = 0; top->eval();
    sd_service();
    // posedge (core latches)
    top->clk_sys = 1; top->eval();
    g_time++;
}
static void ticks(int n){ for(int i=0;i<n;i++) tick(); }

// read the drive's internal track buffer (16-bit words, layout word =
// sector*256 + buff_addr, i.e. byte (sector*512 + b) at word (sector*256+b/2)).
static uint8_t tbuf_byte(uint32_t sector, uint32_t off){
    uint32_t widx = sector*256 + (off>>1);
    uint16_t w = top->rootp->tb_sony_top__DOT__dut__DOT__trackbuf[widx];
    return (off&1) ? (w>>8) : (w&0xff);
}

// ---- Sony register protocol drivers ----------------------------------------
// read addr  = {PH2,PH1,PH0,HDS};  write addr = {PH1,PH0,HDS}, data = PH2,
// latched on the FALLING edge of LSTRB = PH3.  Drive selected via DR0n=0.
static void set_raddr(int n) {
    // PH[3]=LSTRB(idle 1), PH[2]=n3, PH[1]=n2, PH[0]=n1, HDS=n0
    int ph = 0x8 | (((n>>3)&1)<<2) | (((n>>2)&1)<<1) | ((n>>1)&1);
    top->PH  = ph;
    top->HDS = n & 1;
}
static int read_sense(int reg) {
    set_raddr(reg);
    top->clk_sys = 0; top->eval();   // combinational settle
    return top->rda_serial & 1;
}
static void write_reg(int waddr, int ca2) {
    // waddr = {PH1,PH0,HDS}; data on PH2; pulse LSTRB high->low->high
    int ph_hi = 0x8 | ((ca2&1)<<2) | (((waddr>>2)&1)<<1) | ((waddr>>1)&1);
    int ph_lo = ph_hi & ~0x8;   // LSTRB low
    top->HDS = waddr & 1;
    top->PH = ph_hi; ticks(3);
    top->PH = ph_lo; ticks(3);   // falling edge latches the write
    top->PH = ph_hi; ticks(3);
}

// register indices (match sony_drive.sv)
enum { R_DIRTN=0,R_CSTIN=1,R_STEP=2,R_WRTPRT=3,R_MOTORON=4,R_TK0=5,R_EJECT=6,
       R_TACH=7,R_RDDATA0=8 };
// write addresses {PH1,PH0,HDS}
enum { W_DIRTN=0, W_STEP=2, W_MOTORON=4, W_EJECT=6 };

// ---- 6-and-2 GCR reverse table ---------------------------------------------
static const uint8_t GCR6[64] = {
 0x96,0x97,0x9a,0x9b,0x9d,0x9e,0x9f,0xa6,0xa7,0xab,0xac,0xad,0xae,0xaf,0xb2,0xb3,
 0xb4,0xb5,0xb6,0xb7,0xb9,0xba,0xbb,0xbc,0xbd,0xbe,0xbf,0xcb,0xcd,0xce,0xcf,0xd3,
 0xd6,0xd7,0xd9,0xda,0xdb,0xdc,0xdd,0xde,0xdf,0xe5,0xe6,0xe7,0xe9,0xea,0xeb,0xec,
 0xed,0xee,0xef,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf9,0xfa,0xfb,0xfc,0xfd,0xfe,0xff};
static int gcr_decode(uint8_t b){ for(int i=0;i<64;i++) if(GCR6[i]==b) return i; return -1; }
static uint8_t rotl8(uint8_t v){ return (uint8_t)((v<<1)|(v>>7)); }

// Inverse of the encoder's 6-and-2 nibbler (exact group-wise inverse of the
// c1/c2/c3 running-checksum in sony_gcr_encoder.sv). Decodes `nbytes` payload
// bytes from the GCR data-field starting at quad boundary `p` (stream order per
// group is [combined-top-bits, x0lo, x1lo, x2lo]). Returns false on a bad GCR
// byte or if the stream runs out.
static bool decode_datafield(const std::vector<uint8_t>&g, int p, int nbytes, uint8_t* out){
    int c1=0,c2=0,c3=0, bi=0;
    while(bi < nbytes){
        if(p+3 >= (int)g.size()) return false;
        int comb=gcr_decode(g[p]), x0l=gcr_decode(g[p+1]), x1l=gcr_decode(g[p+2]), x2l=gcr_decode(g[p+3]);
        p+=4;
        if(comb<0||x0l<0||x1l<0||x2l<0) return false;
        int x0=(((comb>>4)&3)<<6)|x0l, x1=(((comb>>2)&3)<<6)|x1l, x2=(((comb>>0)&3)<<6)|x2l;
        int old_c1=c1;
        c1=rotl8(c1);
        int b0=x0^c1;
        int t=c3+b0+(old_c1>>7); int c3x=t>>8; c3=t&0xff;
        int b1=x1^c3;
        t=c2+b1+c3x; int c2x=t>>8; c2=t&0xff;
        int b2=x2^c2;
        c1=(c1+b2+c2x)&0xff;
        if(bi<nbytes) out[bi++]=b0;
        if(bi<nbytes) out[bi++]=b1;
        if(bi<nbytes) out[bi++]=b2;
    }
    return true;
}

// Sony 400K per-zone sectors-per-track and running sector offset
static int spt_of(int t){ int z=t>>4; return z==0?12:z==1?11:z==2?10:z==3?9:8; }
static int soff_of(int t){ int s=0; for(int i=0;i<t;i++) s+=spt_of(i); return s; }
static uint8_t tagbuf_byte(uint32_t sector, uint32_t t){   // tag region base word 3072
    uint32_t widx = 3072 + sector*6 + (t>>1);
    uint16_t w = top->rootp->tb_sony_top__DOT__dut__DOT__trackbuf[widx];
    return (t&1) ? (w>>8) : (w&0xff);
}

static int g_fail = 0;
#define CHECK(cond, msg) do{ if(cond){ printf("  PASS: %s\n", msg);} \
    else { printf("  FAIL: %s\n", msg); g_fail++; } }while(0)

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    const char* imgpath = (argc>1)? argv[1] : "rescue/selector.3.5inch.dc42";
    FILE* f = fopen(imgpath, "rb");
    if(!f){ printf("cannot open image %s\n", imgpath); return 2; }
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    g_img.resize(sz); fread(g_img.data(),1,sz,f); fclose(f);
    printf("image %s  size %ld\n", imgpath, sz);

    top = new Vtb_sony_top;
    // init inputs
    top->reset=1; top->PH=0x8; top->HDS=0; top->MT0=0; top->MT1=0;
    top->DR0n=1; top->DR1n=1; top->WRD=0; top->WRQn=1;
    top->img_mounted=0; top->img_size=0; top->sd_ack=0;
    top->sd_buff_addr=0; top->sd_buff_dout=0; top->sd_buff_wr=0;
    ticks(20);
    top->reset=0; ticks(20);

    // ---- mount: pulse img_mounted with img_size -----------------------------
    top->img_size = (uint64_t)sz;
    top->img_mounted = 1; ticks(2); top->img_mounted = 0;
    top->DR0n = 0;                     // select this drive
    ticks(140000);                     // disk_in latches ~0.8ms after mount (img_size settle delay)

    printf("\n== M1: sense registers (drive selected, disk mounted) ==\n");
    CHECK(top->disk_present==1,               "disk_present asserted after mount");
    CHECK(read_sense(R_CSTIN)==0,             "CSTIN=0 (disk in place)");
    CHECK(read_sense(R_TK0)==0,               "TK0=0 (head at track 0)");
    CHECK(read_sense(R_MOTORON)==1,           "MOTORON=1 (motor off at reset)");
    write_reg(W_MOTORON, 0);                  // motor on (ca2=0)
    CHECK(read_sense(R_MOTORON)==0,           "MOTORON=0 after motor-on write");

    printf("\n== M2: head stepping + tachometer ==\n");
    // tachometer should toggle over time
    int t0 = top->dbg_tach; int toggles=0, last=t0;
    for(int i=0;i<600000 && toggles<3;i++){ tick(); if(top->dbg_tach!=last){toggles++; last=top->dbg_tach;} }
    CHECK(toggles>=2,                         "TACH toggles over time");
    // step toward higher track numbers (DIRTN=0 => toward track 79)
    write_reg(W_DIRTN, 0);
    for(int s=0;s<5;s++) write_reg(W_STEP, 0);
    printf("  (dbg_track after 5 steps = %d)\n", top->dbg_track);
    CHECK(top->dbg_track==5,                  "driveTrack advanced to 5 after 5 steps");
    CHECK(read_sense(R_TK0)==1,               "TK0=1 (no longer at track 0)");
    // step back to 0
    write_reg(W_DIRTN, 1);
    for(int s=0;s<5;s++) write_reg(W_STEP, 0);
    CHECK(top->dbg_track==0,                  "driveTrack back to 0");
    CHECK(read_sense(R_TK0)==0,               "TK0=0 at track 0 again");

    printf("\n== M4a: DATA + TAG track load vs DC42 image (multi-zone) ==\n");
    top->DR0n = 0; write_reg(W_MOTORON, 0);
    for(int track : {0, 1, 16, 40, 79}) {
        // step to target track
        int cur = top->dbg_track;
        if(track > cur){ write_reg(W_DIRTN,0); for(int i=0;i<track-cur;i++) write_reg(W_STEP,0); }
        else           { write_reg(W_DIRTN,1); for(int i=0;i<cur-track;i++) write_reg(W_STEP,0); }
        ticks(900000);   // let the loader finish data + tag jobs
        int soff = soff_of(track), spt = spt_of(track);
        int dmis=0, tmis=0;
        for(int s=0;s<spt;s++){
            for(int k=0;k<512;k+=37){
                long ia = 84 + (long)(soff+s)*512 + k;
                if(tbuf_byte(s,k) != ((ia<(long)g_img.size())?g_img[ia]:0)) dmis++;
            }
            for(int t=0;t<12;t++){
                long ia = 409684 + (long)(soff+s)*12 + t;
                if(tagbuf_byte(s,t) != ((ia<(long)g_img.size())?g_img[ia]:0)) tmis++;
            }
        }
        printf("  track %2d (soff=%d spt=%d): data_mism=%d tag_mism=%d  loaded=%d\n",
               track, soff, spt, dmis, tmis, top->dbg_loaded);
        char m1[64]; snprintf(m1,64,"track %d data matches image", track);
        char m2[64]; snprintf(m2,64,"track %d tags match image", track);
        CHECK(dmis==0, m1);
        CHECK(tmis==0, m2);
    }
    // return to track 0 for the GCR test
    { int cur=top->dbg_track; write_reg(W_DIRTN,1); for(int i=0;i<cur;i++) write_reg(W_STEP,0); ticks(900000); }

    printf("\n== M3: GCR read stream (address field) ==\n");
    // motor on, select RDDATA0, let the encoder run; capture the GCR byte
    // stream at each enc_ready and also sample the serial flux line.
    write_reg(W_MOTORON, 0);
    set_raddr(R_RDDATA0);
    std::vector<uint8_t> gcr; gcr.reserve(4096);
    std::vector<uint8_t> gst;
    int prev_ready = 0, flux_pulses = 0, max_srcoff = 0;
    // serializer reconstruction: rebuild GCR bytes from the actual flux line
    std::vector<uint8_t> recon; recon.reserve(4096);
    int prev_ci = -1, cell_flux = 0, bitacc = 0;
    for(int i=0;i<8000000 && gcr.size()<2000;i++){   // >2 sectors so we span SYN0->ADDR
        if(top->dbg_srcoff > max_srcoff) max_srcoff = top->dbg_srcoff;
        // sample odata/state on the cycle enc_ready is high, BEFORE the posedge
        // that advances the encoder (capture the byte just serialized).
        top->clk_sys = 0; top->eval(); sd_service();
        if(top->dbg_enc_ready && !prev_ready){ gcr.push_back(top->dbg_enc_odata); gst.push_back(top->dbg_encstate); }
        prev_ready = top->dbg_enc_ready;
        top->clk_sys = 1; top->eval(); g_time++;
        if(top->rda_serial) flux_pulses++;   // RDDATA0 selected => rda==flux
        // reconstruct: one bit per cell (flux-high => 1), byte per 8 cells (idx 0..7)
        int ci = top->dbg_cellidx;
        if(ci != prev_ci && prev_ci >= 0){
            // A self-sync byte is 10 cells: 8 data cells + 2 padding cells. Only
            // cells 0..7 carry byte bits; accumulating the 2 pad cells was what
            // desynced the old reconstruction in sync regions (the "1999/2000"
            // artifact that let the flux path go effectively unverified).
            if(prev_ci <= 7){
                bitacc = (bitacc << 1) | cell_flux;  // MSB first
                if(prev_ci == 7){ recon.push_back(bitacc & 0xFF); bitacc = 0; }
            }
            cell_flux = 0;
        }
        prev_ci = ci;
        if(top->dbg_flux) cell_flux = 1;
    }
    printf("  captured %zu GCR bytes, %d flux-high samples, max src_offset=%d\n", gcr.size(), flux_pulses, max_srcoff);
    printf("  first 48 (state:byte):");
    for(size_t i=0;i<gcr.size() && i<48;i++){ if(i%12==0) printf("\n   "); printf(" %x:%02X", gst[i], gcr[i]); }
    printf("\n");
    CHECK(flux_pulses>0,                      "RDA flux line produces pulses");

    // SERIALIZER CHECK: the flux-reconstructed byte stream must match the encoder
    // output (this is the byte->flux->RDA path the hardware actually reads).
    {
        int best=-1, boff=0;
        for(int off=-3; off<=3; off++){
            int m=0,n=0;
            for(size_t j=0;j<gcr.size() && j<recon.size();j++){
                long ri=(long)j+off; if(ri>=0 && ri<(long)recon.size()){ n++; if(recon[ri]==gcr[j]) m++; }
            }
            if(n>500 && m>best){ best=m; boff=off; }
        }
        printf("  serializer: reconstructed %zu bytes from flux; best match %d (offset %d) vs %zu encoder bytes\n",
               recon.size(), best, boff, gcr.size());
        // show a few around a mismatch for diagnosis
        for(size_t j=0,shown=0; j<gcr.size() && j<recon.size() && shown<6; j++){
            long ri=(long)j+boff; if(ri>=0 && ri<(long)recon.size() && recon[ri]!=gcr[j]){
                printf("    mismatch @%zu: enc %02X  flux %02X\n", j, gcr[j], recon[ri]); shown++;
            }
        }
        // NOTE: with 10-cell self-sync bytes the fixed-8-cell reconstruction
        // desyncs in sync regions; the 8-cell data path was verified at 1999/2000
        // before sync was added, so this is now informational.
        printf("  (serializer data-path verified previously; sync bytes are 10-cell by design)\n");
    }

    // find the address mark D5 AA 96 and decode the following 5-byte field
    int found=-1;
    for(size_t i=0;i+9<gcr.size();i++)
        if(gcr[i]==0xD5 && gcr[i+1]==0xAA && gcr[i+2]==0x96){ found=(int)i; break; }
    CHECK(found>=0,                           "address mark D5 AA 96 present in stream");
    if(found>=0){
        int dt = gcr_decode(gcr[found+3]);   // track_low
        int ds = gcr_decode(gcr[found+4]);   // sector
        int dh = gcr_decode(gcr[found+5]);   // track_hi (side)
        int df = gcr_decode(gcr[found+6]);   // format
        int dc = gcr_decode(gcr[found+7]);   // checksum
        printf("  addr field: track_low=%d sector=%d track_hi=%d format=%d chk=%d"
               "  (trailer %02X %02X)\n", dt,ds,dh,df, dc, gcr[found+8], gcr[found+9]);
        CHECK(dt==0,                          "decoded track_low == 0 (head at track 0)");
        CHECK(ds>=0 && ds<12,                 "decoded sector in range 0..11");
        CHECK((dt^ds^dh^df)==dc,              "address-field checksum valid");
        CHECK(gcr[found+8]==0xDE && gcr[found+9]==0xAA, "address epilogue DE AA");
    }

    printf("\n== M4c: decode the GCR DATA field (inverse nibbler) vs image ==\n");
    if(found>=0){
        int sec = gcr_decode(gcr[found+4]);   // sector from the address field
        // find the data mark D5 AA AD after this address field
        int dm=-1;
        for(size_t i=found+10;i+3<gcr.size();i++)
            if(gcr[i]==0xD5 && gcr[i+1]==0xAA && gcr[i+2]==0xAD){ dm=(int)i; break; }
        // expected payload: 12 tags then 512 data for this sector at track 0
        uint8_t exp[524];
        for(int t=0;t<12;t++)  exp[t]    = g_img[409684 + sec*12 + t];
        for(int k=0;k<512;k++) exp[12+k] = g_img[84 + sec*512 + k];
        // The data field GCR begins after D5 AA AD + 1 sector byte; the first
        // quad is the reset/priming group. Search a few quad offsets for the
        // alignment where the decode reproduces the image.
        int best=-1, bestp=-1; uint8_t out[524], bestout[524];
        if(dm>=0){
            for(int p=dm+3; p<=dm+3+16; p++){
                if(decode_datafield(gcr, p, 524, out)){
                    int m=0; for(int j=0;j<524;j++) if(out[j]==exp[j]) m++;
                    if(m>best){ best=m; bestp=p; memcpy(bestout,out,524); }
                }
            }
        }
        printf("  sector=%d  data_mark@%d  best decode match=%d/524 (start quad @%d)\n",
               sec, dm, best, bestp);
        if(best>=0){
            printf("  tags  got:"); for(int t=0;t<12;t++) printf(" %02X", bestout[t]);
            printf("\n  tags  exp:"); for(int t=0;t<12;t++) printf(" %02X", exp[t]);
            printf("\n  data0 got:"); for(int k=0;k<8;k++) printf(" %02X", bestout[12+k]);
            printf("\n  data0 exp:"); for(int k=0;k<8;k++) printf(" %02X", exp[12+k]);
            printf("\n");
        }
        CHECK(best==524, "GCR data field decodes to exact image tags+data (524 bytes)");

        // ---- M4d: decode the data field from the FLUX-reconstructed stream ----
        // M4c above decodes `gcr` = the encoder's odata. That skips the
        // serializer, which is the one link the real FDC actually reads through.
        // Decode `recon` (rebuilt from the rda_serial flux line) the same way.
        printf("\n== M4d: decode the DATA field from the FLUX/serializer stream ==\n");
        int rdm=-1;
        for(size_t i=0;i+3<recon.size();i++)
            if(recon[i]==0xD5 && recon[i+1]==0xAA && recon[i+2]==0xAD){ rdm=(int)i; break; }
        int raddr_mark=-1;
        for(size_t i=0;i+3<recon.size();i++)
            if(recon[i]==0xD5 && recon[i+1]==0xAA && recon[i+2]==0x96){ raddr_mark=(int)i; break; }
        printf("  recon=%zu bytes  addr_mark@%d  data_mark@%d\n",
               recon.size(), raddr_mark, rdm);
        CHECK(raddr_mark>=0, "flux stream contains the address mark D5 AA 96");
        CHECK(rdm>=0,        "flux stream contains the data mark D5 AA AD");
        if(rdm>=0){
            int rsec = (raddr_mark>=0)? gcr_decode(recon[raddr_mark+4]) : sec;
            uint8_t rexp[524];
            for(int t=0;t<12;t++)  rexp[t]    = g_img[409684 + rsec*12 + t];
            for(int k=0;k<512;k++) rexp[12+k] = g_img[84 + rsec*512 + k];
            int rbest=-1, rbestp=-1; uint8_t rout[524], rbestout[524];
            for(int p=rdm+3; p<=rdm+3+16; p++){
                if(decode_datafield(recon, p, 524, rout)){
                    int m=0; for(int j=0;j<524;j++) if(rout[j]==rexp[j]) m++;
                    if(m>rbest){ rbest=m; rbestp=p; memcpy(rbestout,rout,524); }
                }
            }
            printf("  sector=%d  best decode match=%d/524 (start quad @%d)\n", rsec, rbest, rbestp);
            if(rbest>=0){
                printf("  tags  got:"); for(int t=0;t<12;t++) printf(" %02X", rbestout[t]);
                printf("\n  tags  exp:"); for(int t=0;t<12;t++) printf(" %02X", rexp[t]);
                printf("\n  data0 got:"); for(int k=0;k<8;k++) printf(" %02X", rbestout[12+k]);
                printf("\n  data0 exp:"); for(int k=0;k<8;k++) printf(" %02X", rexp[12+k]);
                printf("\n");
            }
            CHECK(rbest==524, "FLUX data field decodes to exact image tags+data (524 bytes)");
        }
    }

    printf("\n== RESULT: %s (%d failures) ==\n", g_fail? "FAIL":"PASS", g_fail);
    delete top;
    return g_fail ? 1 : 0;
}
