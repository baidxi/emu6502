/*
 * NES Emulator - PPU Simulator
 * Fixes: NMI edge trigger, $2007 mirroring, attribute shift, coarse_x per-tile
 */
#include "ppu.h"
#include "bus.h"
#include <string.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ppu, CONFIG_LOG_DEFAULT_LEVEL);

static const uint32_t nes_palette[64] = {
    0x666666, 0x002A88, 0x1412A7, 0x3B00A4, 0x5C007E, 0x6E0040, 0x6C0600,
    0x561D00, 0x333500, 0x0B4800, 0x005200, 0x004F08, 0x00404D, 0x000000,
    0x000000, 0x000000, 0xADADAD, 0x155FD9, 0x4240FF, 0x7527FE, 0xA01ACC,
    0xB71E7B, 0xB53120, 0x994E00, 0x6B6D00, 0x388700, 0x0C9300, 0x008F32,
    0x007C8D, 0x000000, 0x000000, 0x000000, 0xFFFEFF, 0x64B0FF, 0x9290FF,
    0xC676FF, 0xF36AFF, 0xFE6ECC, 0xFE8170, 0xEA9E22, 0xBCBE00, 0x88D800,
    0x5CE430, 0x45E082, 0x48CDDE, 0x4F4F4F, 0x000000, 0x000000, 0xFFFEFF,
    0xC0DFFF, 0xD3D2FF, 0xE8C8FF, 0xFBC2FF, 0xFEC4EA, 0xFECCC5, 0xF7D8A5,
    0xE4E594, 0xCFEF96, 0xBDF4AB, 0xB3F3CC, 0xB5EBF2, 0xB8B8B8, 0x000000,
    0x000000,
};

uint32_t ppu_palette_color(uint8_t i) {
  i &= 0x3F;
  return nes_palette[i];
}

void ppu_init(nes_ppu_t *ppu) {
  memset(ppu, 0, sizeof(nes_ppu_t));
  ppu->status = 0x00;
  /* 双缓冲: render_buf = 后台渲染, display_buf = 前台显示 */
  ppu->render_buf = ppu->frame_buffers[0];
  ppu->display_buf = ppu->frame_buffers[1];
  for (int b = 0; b < 2; b++)
    for (int i = 0; i < NES_SCREEN_WIDTH * NES_SCREEN_HEIGHT; i++)
      ppu->frame_buffers[b][i] = 0xFF000000;
}

uint8_t ppu_read_reg(nes_ppu_t *ppu, struct nes_bus *bus, uint16_t addr) {
  switch (addr) {
  case 0x2002: {
    uint8_t r = ppu->status;
    ppu->status &= ~0x80;
    ppu->latch = 0;
    return r;
  }
  case 0x2004:
    return ppu->oam[ppu->oam_addr];
  case 0x2007: {
    uint8_t r = ppu->data_buf;
    uint16_t vaddr = ppu->v & 0x3FFF;
    if (vaddr < 0x2000)
      ppu->data_buf = bus_ppu_read(bus, vaddr); /* CHR-ROM 走 bus */
    else {
      ppu->data_buf = ppu_read_vram(ppu, vaddr);
      if ((vaddr & 0x3F00) == 0x3F00)
        r = ppu->data_buf;
    }
    ppu->v += (ppu->ctrl & 0x04) ? 32 : 1;
    return r;
  }
  default:
    return 0;
  }
}

void ppu_write_reg(nes_ppu_t *ppu, struct nes_bus *bus, uint16_t addr,
                   uint8_t data) {
  switch (addr) {
  case 0x2000: {
    bool old = ppu->nmi_output;
    ppu->ctrl = data;
    ppu->nmi_output = (data & 0x80) != 0;
    if (!old && ppu->nmi_output && (ppu->status & 0x80))
      ppu->nmi_occurred = true;
    ppu->t = (ppu->t & 0xF3FF) | (((uint16_t)(data & 0x03)) << 10);
  } break;
  case 0x2001:
    ppu->mask = data;
    break;
  case 0x2003:
    ppu->oam_addr = data;
    break;
  case 0x2004:
    ppu->oam[ppu->oam_addr++] = data;
    break;
  case 0x2005:
    if (!ppu->latch) {
      ppu->fine_x = data & 0x07;
      ppu->t = (ppu->t & 0xFFE0) | ((data >> 3) & 0x1F);
      ppu->latch = 1;
    } else {
      ppu->t = (ppu->t & 0x0C1F) | (((uint16_t)(data & 0x07)) << 12) |
               (((uint16_t)(data & 0xF8)) << 2);
      ppu->latch = 0;
    }
    break;
  case 0x2006:
    if (!ppu->latch) {
      ppu->t = (ppu->t & 0x00FF) | (((uint16_t)(data & 0x3F)) << 8);
      ppu->latch = 1;
    } else {
      ppu->t = (ppu->t & 0xFF00) | data;
      ppu->v = ppu->t;
      ppu->latch = 0;
    }
    break;
  case 0x2007: {
    uint16_t waddr = ppu->v & 0x3FFF;
    if ((waddr & 0x3F00) == 0x3F00)
      ppu_write_vram(ppu, waddr, data);
    else
      bus_ppu_write(bus, waddr, data);
    ppu->v += (ppu->ctrl & 0x04) ? 32 : 1;
  } break;
  case 0x4014:
    break;
  }
}

uint8_t ppu_read_vram(nes_ppu_t *ppu, uint16_t addr) {
  addr &= 0x3FFF;
  if (addr < 0x2000)
    return 0;
  if (addr < 0x3F00)
    return ppu->vram[addr & 0x0FFF];
  addr &= 0x1F;
  if (addr >= 0x10 && !(addr & 3))
    addr -= 0x10;
  return ppu->palette[addr];
}

void ppu_write_vram(nes_ppu_t *ppu, uint16_t addr, uint8_t data) {
  addr &= 0x3FFF;
  if (addr < 0x2000)
    return;
  if (addr < 0x3F00) {
    ppu->vram[addr & 0x0FFF] = data;
    return;
  }
  addr &= 0x1F;
  if (addr >= 0x10 && !(addr & 3))
    addr -= 0x10;
  ppu->palette[addr] = data;
}

static uint8_t get_nt(nes_ppu_t *ppu, struct nes_bus *bus, uint16_t nt,
                      uint8_t tx, uint8_t ty) {
  return bus_ppu_read(bus, nt + ty * 32 + tx);
}
static uint16_t get_ptr(nes_ppu_t *ppu, struct nes_bus *bus, uint8_t ti,
                        uint8_t row, uint16_t pb) {
  uint16_t a = pb + ti * 16 + row;
  uint8_t p0 = bus_ppu_read(bus, a), p1 = bus_ppu_read(bus, a + 8);
  uint16_t r = 0;
  for (int i = 0; i < 8; i++) {
    uint8_t p = ((p0 >> (7 - i)) & 1) | (((p1 >> (7 - i)) & 1) << 1);
    r = (r << 2) | p;
  }
  return r;
}

static void render_bg(nes_ppu_t *ppu, struct nes_bus *bus, int sl) {
  uint16_t pb = (ppu->ctrl & 0x10) ? 0x1000 : 0x0000;

  /* 水平重载: coarse_x(4-0) 和 nh(10) 从 t 重载到 v
   * v = (v & ~0x041F) | (t & 0x041F) */
  ppu->v = (ppu->v & ~0x041F) | (ppu->t & 0x041F);

  uint8_t cx = ppu->v & 0x1F, cy = (ppu->v >> 5) & 0x1F,
          fy = (ppu->v >> 12) & 0x07;
  uint8_t nh = (ppu->v >> 10) & 1, nv = (ppu->v >> 11) & 1;
  uint8_t tr = (sl + fy) / 8, pr = (sl + fy) % 8;
  /* 计算 tile row 对应的 coarse_y 和垂直命名表位,
   * NES PPU 在 coarse_y 到达 30 时折返并翻转 nv (不是32) */
  uint8_t row_cy = cy, row_nv = nv;
  for (int i = 0; i < tr; i++) {
    if (++row_cy == 30) {
      row_cy = 0;
      row_nv ^= 1;
    }
  }

  for (int tx = 0; tx < 33; tx++) {
    uint8_t nx = cx, ny = row_cy;
    uint16_t nt = 0x2000 | ((uint16_t)nh << 10) | ((uint16_t)row_nv << 11);
    uint8_t tid = get_nt(ppu, bus, nt, nx, ny);
    uint16_t px = get_ptr(ppu, bus, tid, pr, pb);
    uint16_t aa = nt + 0x03C0 + (ny / 4) * 8 + (nx / 4);
    uint8_t at = bus_ppu_read(bus, aa);
    uint8_t as = (nx & 2) | ((ny & 2) << 1);
    uint8_t phi = (at >> as) & 3;
    int sx = tx * 8 - ppu->fine_x;
    for (int px2 = 0; px2 < 8; px2++) {
      int x = sx + px2;
      if (x < 0 || x >= NES_SCREEN_WIDTH)
        continue;
      uint8_t ci = (px >> ((7 - px2) * 2)) & 3;
      ci = ci ? ppu->palette[phi * 4 + ci] & 0x3F : ppu->palette[0] & 0x3F;
      ppu->render_buf[sl * NES_SCREEN_WIDTH + x] = nes_palette[ci];
    }
    /* 更新 coarse_x 和 nh (在真实PPU中是逐tile更新的) */
    if (++cx == 32) {
      cx = 0;
      nh ^= 1;
    }
    ppu->v = (ppu->v & ~0x041F) | (cx & 0x1F) | ((uint16_t)nh << 10);
  }
}

static void render_sp(nes_ppu_t *ppu, struct nes_bus *bus, int sl) {
  bool s16 = (ppu->ctrl & 0x20) != 0;
  uint16_t spb = (ppu->ctrl & 0x08) ? 0x1000 : 0x0000;
  int cnt = 0, vis[8];
  for (int i = 0; i < 64 && cnt < 8; i++) {
    uint8_t sy = ppu->oam[i * 4 + 0] + 1;
    int sh = s16 ? 16 : 8;
    if (sl >= sy && sl < sy + sh)
      vis[cnt++] = i;
  }
  for (int s = cnt - 1; s >= 0; s--) {
    int idx = vis[s];
    uint8_t sy = ppu->oam[idx * 4 + 0] + 1, ti = ppu->oam[idx * 4 + 1];
    uint8_t at = ppu->oam[idx * 4 + 2], sx = ppu->oam[idx * 4 + 3];
    bool fh = at & 0x40, fv = at & 0x80, bg = at & 0x20;
    uint8_t pi = (at & 3) + 4;
    int row = sl - sy;
    if (fv)
      row = (s16 ? 15 : 7) - row;
    if (s16) {
      if (row >= 8) {
        ti = (ti & 0xFE) | ((ti & 1) ^ 1);
        row -= 8;
      }
      ti &= 0xFE;
    }
    uint16_t px = get_ptr(ppu, bus, ti, row, spb);
    for (int px2 = 0; px2 < 8; px2++) {
      int x = sx + (fh ? 7 - px2 : px2);
      if (x < 0 || x >= NES_SCREEN_WIDTH)
        continue;
      uint8_t ci = (px >> ((7 - px2) * 2)) & 3;
      if (!ci)
        continue;
      if (idx == 0 && sx < 255 &&
          (ppu->render_buf[sl * NES_SCREEN_WIDTH + x] != 0xFF000000))
        ppu->status |= 0x40;
      if (bg && (ppu->render_buf[sl * NES_SCREEN_WIDTH + x] != 0xFF000000))
        continue;
      ci = ppu->palette[pi * 4 + ci] & 0x3F;
      ppu->render_buf[sl * NES_SCREEN_WIDTH + x] = nes_palette[ci];
    }
  }
}

void ppu_process_scanline(nes_ppu_t *ppu, struct nes_bus *bus, int sl) {
  ppu->scanline = sl;
  if (sl == 241) {
    ppu->status |= 0x80;
    ppu->nmi_occurred = true;
    ppu->frame_complete = true;
    return;
  }
  if (sl == 261) {
    /* 预渲染扫描线: 垂直滚动位从 t 重载到 v
     * v = (v & ~0x7BE0) | (t & 0x7BE0)
     * 重载: fine_y(14-12), nv(11), coarse_y(9-5) */
    ppu->v = (ppu->v & ~0x7BE0) | (ppu->t & 0x7BE0);
    ppu->status &= ~0x80;
    ppu->status &= ~0x40;
    ppu->nmi_occurred = false;
    ppu->frame_complete = false;
    return;
  }
  if (sl >= 240)
    return;
  if (!(ppu->mask & 0x08) && !(ppu->mask & 0x10))
    return;

  /* 填充背景色 */
  if (sl == 0) {
    uint32_t bg = nes_palette[ppu->palette[0] & 0x3F];
    for (int i = 0; i < NES_SCREEN_WIDTH * NES_SCREEN_HEIGHT; i++)
      ppu->render_buf[i] = bg;
  }

  if (ppu->mask & 0x08)
    render_bg(ppu, bus, sl);
  if (ppu->mask & 0x10)
    render_sp(ppu, bus, sl);
}
