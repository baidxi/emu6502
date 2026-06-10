/*
 * NES Emulator - PPU Simulator
 * Fixes: NMI edge trigger, $2007 mirroring, attribute shift, coarse_x per-tile
 */
#include <string.h>
#include <zephyr/logging/log.h>
#include "ppu.h"
#include "bus.h"
LOG_MODULE_REGISTER(ppu, CONFIG_LOG_DEFAULT_LEVEL);

static const uint32_t nes_palette[64] = {
    0x666666,0x002A88,0x1412A7,0x3B00A4,0x5C007E,0x6E0040,0x6C0600,0x561D00,
    0x333500,0x0B4800,0x005200,0x004F08,0x00404D,0x000000,0x000000,0x000000,
    0xADADAD,0x155FD9,0x4240FF,0x7527FE,0xA01ACC,0xB71E7B,0xB53120,0x994E00,
    0x6B6D00,0x388700,0x0C9300,0x008F32,0x007C8D,0x000000,0x000000,0x000000,
    0xFFFEFF,0x64B0FF,0x9290FF,0xC676FF,0xF36AFF,0xFE6ECC,0xFE8170,0xEA9E22,
    0xBCBE00,0x88D800,0x5CE430,0x45E082,0x48CDDE,0x4F4F4F,0x000000,0x000000,
    0xFFFEFF,0xC0DFFF,0xD3D2FF,0xE8C8FF,0xFBC2FF,0xFEC4EA,0xFECCC5,0xF7D8A5,
    0xE4E594,0xCFEF96,0xBDF4AB,0xB3F3CC,0xB5EBF2,0xB8B8B8,0x000000,0x000000,
};

uint32_t ppu_palette_color(uint8_t i){i&=0x3F;return nes_palette[i];}

void ppu_init(nes_ppu_t *ppu){
    memset(ppu,0,sizeof(nes_ppu_t));
    ppu->status=0x00;
    /* 双缓冲: render_buf = 后台渲染, display_buf = 前台显示 */
    ppu->render_buf  = ppu->frame_buffers[0];
    ppu->display_buf = ppu->frame_buffers[1];
    for(int b=0;b<2;b++)
        for(int i=0;i<NES_SCREEN_WIDTH*NES_SCREEN_HEIGHT;i++)
            ppu->frame_buffers[b][i]=0xFF000000;
}

uint8_t ppu_read_reg(nes_ppu_t *ppu, uint16_t addr){
    switch(addr){
    case 0x2002:
        {uint8_t r=ppu->status;ppu->status&=~0x80;ppu->latch=0;return r;}
    case 0x2004: return ppu->oam[ppu->oam_addr];
    case 0x2007:
        {uint8_t r=ppu->data_buf;
         ppu->data_buf=ppu_read_vram(ppu,ppu->v&0x3FFF);
         if((ppu->v&0x3F00)==0x3F00) r=ppu->data_buf;
         ppu->v+=(ppu->ctrl&0x04)?32:1;
         return r;}
    default: return 0;
    }
}

void ppu_write_reg(nes_ppu_t *ppu, struct nes_bus *bus, uint16_t addr, uint8_t data){
    switch(addr){
    case 0x2000:
        {bool old=ppu->nmi_output;ppu->ctrl=data;
         ppu->nmi_output=(data&0x80)!=0;
         if(!old&&ppu->nmi_output&&(ppu->status&0x80)) ppu->nmi_occurred=true;
         ppu->t=(ppu->t&0xF3FF)|(((uint16_t)(data&0x03))<<10);}
        break;
    case 0x2001: ppu->mask=data; break;
    case 0x2003: ppu->oam_addr=data; break;
    case 0x2004: ppu->oam[ppu->oam_addr++]=data; break;
    case 0x2005:
        if(!ppu->latch){ppu->fine_x=data&0x07;ppu->t=(ppu->t&0xFFE0)|((data>>3)&0x1F);ppu->latch=1;}
        else{
             ppu->t=(ppu->t&0x0C1F)|(((uint16_t)(data&0x07))<<12)|(((uint16_t)(data&0xF8))<<2);
             ppu->latch=0;
        }
        break;
    case 0x2006:
        if(!ppu->latch){ppu->t=(ppu->t&0x00FF)|(((uint16_t)(data&0x3F))<<8);ppu->latch=1;}
        else{ppu->t=(ppu->t&0xFF00)|data;ppu->v=ppu->t;ppu->latch=0;}
        break;
    case 0x2007:
        if((ppu->v&0x3F00)==0x3F00) ppu_write_vram(ppu,ppu->v&0x3FFF,data);
        else bus_ppu_write(bus,ppu->v&0x3FFF,data);
        ppu->v+=(ppu->ctrl&0x04)?32:1;
        break;
    case 0x4014: break;
    }
}

uint8_t ppu_read_vram(nes_ppu_t *ppu, uint16_t addr){
    addr&=0x3FFF;
    if(addr<0x2000) return 0;
    if(addr<0x3F00) return ppu->vram[addr&0x0FFF];
    addr&=0x1F;if(addr>=0x10&&!(addr&3))addr-=0x10;
    return ppu->palette[addr];
}

void ppu_write_vram(nes_ppu_t *ppu, uint16_t addr, uint8_t data){
    addr&=0x3FFF;
    if(addr<0x2000) return;
    if(addr<0x3F00){ppu->vram[addr&0x0FFF]=data;return;}
    addr&=0x1F;if(addr>=0x10&&!(addr&3))addr-=0x10;
    ppu->palette[addr]=data;
}

static uint8_t get_nt(nes_ppu_t *ppu, struct nes_bus *bus, uint16_t nt, uint8_t tx, uint8_t ty){
    return bus_ppu_read(bus, nt+ty*32+tx);
}
static uint16_t get_ptr(nes_ppu_t *ppu, struct nes_bus *bus, uint8_t ti, uint8_t row, uint16_t pb){
    uint16_t a=pb+ti*16+row;
    uint8_t p0=bus_ppu_read(bus,a),p1=bus_ppu_read(bus,a+8);
    uint16_t r=0;
    for(int i=0;i<8;i++){uint8_t p=((p0>>(7-i))&1)|(((p1>>(7-i))&1)<<1);r=(r<<2)|p;}
    return r;
}

static void render_bg(nes_ppu_t *ppu, struct nes_bus *bus, int sl){
    uint16_t pb=(ppu->ctrl&0x10)?0x1000:0x0000;
    uint8_t cx=ppu->t&0x1F,cy=(ppu->t>>5)&0x1F,fy=(ppu->t>>12)&0x07;
    uint8_t nh=(ppu->t>>10)&1,nv=(ppu->t>>11)&1;
    uint8_t tr=(sl+fy)/8,pr=(sl+fy)%8;
    /* 计算 tile row 对应的 coarse_y 和垂直命名表位,
     * NES PPU 在 coarse_y 到达 30 时折返并翻转 nv (不是32) */
    uint8_t row_cy=cy,row_nv=nv;
    for(int i=0;i<tr;i++){if(++row_cy==30){row_cy=0;row_nv^=1;}}

    /* ===== 诊断日志: 在 frame 100 和 101 的 scanline 0 打印完整状态 ===== */
    static int diag_frame=0;
    if(sl==0 && diag_frame>=100 && diag_frame<102){
        LOG_ERR("=== DIAG f=%d sl=%d ===", diag_frame, sl);
        /* 假设A: t vs v 寄存器对比 */
        LOG_ERR("[REGS] t=%04X v=%04X fine_x=%d mask=%02X ctrl=%02X",
                ppu->t, ppu->v, ppu->fine_x, ppu->mask, ppu->ctrl);
        LOG_ERR("[SCROLL] cx=%d cy=%d fy=%d nh=%d nv=%d row_cy=%d row_nv=%d",
                cx, cy, fy, nh, nv, row_cy, row_nv);

        /* 假设B: 调色板数据 */
        LOG_ERR("[PAL] %02X %02X %02X %02X | %02X %02X %02X %02X | %02X %02X %02X %02X | %02X %02X %02X %02X",
                ppu->palette[0],ppu->palette[1],ppu->palette[2],ppu->palette[3],
                ppu->palette[4],ppu->palette[5],ppu->palette[6],ppu->palette[7],
                ppu->palette[8],ppu->palette[9],ppu->palette[10],ppu->palette[11],
                ppu->palette[12],ppu->palette[13],ppu->palette[14],ppu->palette[15]);
        LOG_ERR("[PAL+] %02X %02X %02X %02X | %02X %02X %02X %02X | %02X %02X %02X %02X | %02X %02X %02X %02X",
                ppu->palette[16],ppu->palette[17],ppu->palette[18],ppu->palette[19],
                ppu->palette[20],ppu->palette[21],ppu->palette[22],ppu->palette[23],
                ppu->palette[24],ppu->palette[25],ppu->palette[26],ppu->palette[27],
                ppu->palette[28],ppu->palette[29],ppu->palette[30],ppu->palette[31]);

        /* OAM 前8个精灵 */
        LOG_ERR("[OAM] s0:y=%02X t=%02X a=%02X x=%02X s1:y=%02X t=%02X a=%02X x=%02X",
                ppu->oam[0],ppu->oam[1],ppu->oam[2],ppu->oam[3],
                ppu->oam[4],ppu->oam[5],ppu->oam[6],ppu->oam[7]);
        LOG_ERR("[OAM] s2:y=%02X t=%02X a=%02X x=%02X s3:y=%02X t=%02X a=%02X x=%02X",
                ppu->oam[8],ppu->oam[9],ppu->oam[10],ppu->oam[11],
                ppu->oam[12],ppu->oam[13],ppu->oam[14],ppu->oam[15]);
        LOG_ERR("[OAM] s4:y=%02X t=%02X a=%02X x=%02X s5:y=%02X t=%02X a=%02X x=%02X",
                ppu->oam[16],ppu->oam[17],ppu->oam[18],ppu->oam[19],
                ppu->oam[20],ppu->oam[21],ppu->oam[22],ppu->oam[23]);
        LOG_ERR("[OAM] s6:y=%02X t=%02X a=%02X x=%02X s7:y=%02X t=%02X a=%02X x=%02X",
                ppu->oam[24],ppu->oam[25],ppu->oam[26],ppu->oam[27],
                ppu->oam[28],ppu->oam[29],ppu->oam[30],ppu->oam[31]);

        /* VRAM 数据: NT0 行 0,5,10,15,20,25 */
        uint8_t *v=ppu->vram;
        LOG_ERR("[NT0] r0:%02X%02X%02X%02X%02X%02X%02X%02X..%02X",
                v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[31]);
        LOG_ERR("[NT0] r5:%02X%02X%02X%02X..%02X r10:%02X%02X%02X%02X..%02X",
                v[5*32],v[5*32+1],v[5*32+2],v[5*32+3],v[5*32+31],
                v[10*32],v[10*32+1],v[10*32+2],v[10*32+3],v[10*32+31]);
        LOG_ERR("[NT0] r15:%02X%02X%02X%02X..%02X r20:%02X%02X%02X%02X..%02X",
                v[15*32],v[15*32+1],v[15*32+2],v[15*32+3],v[15*32+31],
                v[20*32],v[20*32+1],v[20*32+2],v[20*32+3],v[20*32+31]);
        LOG_ERR("[NT0] r25:%02X%02X%02X%02X..%02X r29:%02X%02X%02X%02X..%02X",
                v[25*32],v[25*32+1],v[25*32+2],v[25*32+3],v[25*32+31],
                v[29*32],v[29*32+1],v[29*32+2],v[29*32+3],v[29*32+31]);
        /* NT1 行 0,5 */
        LOG_ERR("[NT1] r0:%02X%02X%02X%02X..%02X r5:%02X%02X%02X%02X..%02X",
                v[0x400],v[0x401],v[0x402],v[0x403],v[0x41F],
                v[0x400+5*32],v[0x400+5*32+1],v[0x400+5*32+2],v[0x400+5*32+3],v[0x400+5*32+31]);
        /* 属性表 */
        LOG_ERR("[ATTR0] %02X%02X%02X%02X..%02X  [ATTR1] %02X%02X%02X%02X..%02X",
                v[0x3C0],v[0x3C1],v[0x3C2],v[0x3C3],v[0x3FF],
                v[0x7C0],v[0x7C1],v[0x7C2],v[0x7C3],v[0x7FF]);
    }
    /* 渲染 tile 转储: frame 100 scanline 16 的前 16 个 tile */
    if(sl==16 && diag_frame==100){
        LOG_ERR("[TILES sl=16] row_cy=%d row_nv=%d pr=%d pb=%04X", row_cy, row_nv, pr, pb);
    }

    for(int tx=0;tx<33;tx++){
        uint8_t nx=cx,ny=row_cy;
        uint16_t nt=0x2000|((uint16_t)nh<<10)|((uint16_t)row_nv<<11);
        uint8_t tid=get_nt(ppu,bus,nt,nx,ny);
        uint16_t px=get_ptr(ppu,bus,tid,pr,pb);
        uint16_t aa=nt+0x03C0+(ny/4)*8+(nx/4);
        uint8_t at=bus_ppu_read(bus,aa);
        uint8_t as=(nx&2)|((ny&2)<<1);
        uint8_t phi=(at>>as)&3;
        /* 渲染 tile 转储: frame 100 scanline 16 前 16 tile */
        if(sl==16 && diag_frame==100 && tx<16){
            uint8_t ci0=(px>>14)&3, ci4=(px>>6)&3;
            LOG_ERR("  tx%02d: nt=%04X nx=%d ny=%d tid=%02X at=%02X phi=%d ci[0]=%d ci[4]=%d pal=%02X%02X%02X%02X",
                    tx, nt, nx, ny, tid, at, phi, ci0, ci4,
                    ppu->palette[phi*4+0], ppu->palette[phi*4+1],
                    ppu->palette[phi*4+2], ppu->palette[phi*4+3]);
        }
        int sx=tx*8-ppu->fine_x;
        for(int px2=0;px2<8;px2++){
            int x=sx+px2;
            if(x<0||x>=NES_SCREEN_WIDTH)continue;
            uint8_t ci=(px>>((7-px2)*2))&3;
            ci=ci?ppu->palette[phi*4+ci]&0x3F:ppu->palette[0]&0x3F;
            ppu->render_buf[sl*NES_SCREEN_WIDTH+x]=nes_palette[ci];
        }
        if(++cx==32){cx=0;nh^=1;}
    }
}

static void render_sp(nes_ppu_t *ppu, struct nes_bus *bus, int sl){
    bool s16=(ppu->ctrl&0x20)!=0;
    uint16_t spb=(ppu->ctrl&0x08)?0x1000:0x0000;
    int cnt=0,vis[8];
    for(int i=0;i<64&&cnt<8;i++){
        uint8_t sy=ppu->oam[i*4+0]+1;
        int sh=s16?16:8;
        if(sl>=sy&&sl<sy+sh) vis[cnt++]=i;
    }
    /* 精灵诊断: frame 100 scanline 16 */
    static int sp_diag_frame=0;
    if(sl==16 && sp_diag_frame==100){
        LOG_ERR("[SP sl=16] cnt=%d s16=%d spb=%04X", cnt, s16, spb);
        for(int s=0;s<cnt;s++){
            int idx=vis[s];
            LOG_ERR("  sp[%d]: idx=%d y=%02X t=%02X a=%02X x=%02X",
                    s, idx, ppu->oam[idx*4+0], ppu->oam[idx*4+1],
                    ppu->oam[idx*4+2], ppu->oam[idx*4+3]);
        }
    }
    if(sl==239) sp_diag_frame++;

    for(int s=cnt-1;s>=0;s--){
        int idx=vis[s];
        uint8_t sy=ppu->oam[idx*4+0]+1,ti=ppu->oam[idx*4+1];
        uint8_t at=ppu->oam[idx*4+2],sx=ppu->oam[idx*4+3];
        bool fh=at&0x40,fv=at&0x80,bg=at&0x20;
        uint8_t pi=(at&3)+4;
        int row=sl-sy;if(fv)row=(s16?15:7)-row;
        if(s16){if(row>=8){ti=(ti&0xFE)|((ti&1)^1);row-=8;}ti&=0xFE;}
        uint16_t px=get_ptr(ppu,bus,ti,row,spb);
        for(int px2=0;px2<8;px2++){
            int x=sx+(fh?7-px2:px2);
            if(x<0||x>=NES_SCREEN_WIDTH)continue;
            uint8_t ci=(px>>((7-px2)*2))&3;
            if(!ci)continue;
            if(idx==0&&sx<255&&(ppu->render_buf[sl*NES_SCREEN_WIDTH+x]!=0xFF000000))
                ppu->status|=0x40;
            if(bg&&(ppu->render_buf[sl*NES_SCREEN_WIDTH+x]!=0xFF000000))continue;
            ci=ppu->palette[pi*4+ci]&0x3F;
            ppu->render_buf[sl*NES_SCREEN_WIDTH+x]=nes_palette[ci];
        }
    }
}

void ppu_process_scanline(nes_ppu_t *ppu, struct nes_bus *bus, int sl){
    ppu->scanline=sl;
    if(sl==241){
        ppu->status|=0x80;ppu->nmi_occurred=true;ppu->frame_complete=true;
        return;
    }
    if(sl==261){ppu->status&=~0x80;ppu->status&=~0x40;ppu->nmi_occurred=false;ppu->frame_complete=false;return;}
    if(sl>=240)return;
    if(!(ppu->mask&0x08)&&!(ppu->mask&0x10))return;
    if(ppu->mask&0x08)render_bg(ppu,bus,sl);
    if(ppu->mask&0x10)render_sp(ppu,bus,sl);
    /* 逐行 fine_y++ (只在可见行和预渲染行) */
}
