/*
 * NES Emulator - 6502 CPU Simulator
 *
 * 实现 MOS 6502 全部 56 条官方指令 (151 个合法 opcode)。
 * 采用操作码跳转表 (opcode dispatch table) 方式。
 *
 * 寻址模式返回操作数地址，执行函数使用该地址进行读写。
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/logging/log.h>
#include "cpu_6502.h"
#include "bus.h"

LOG_MODULE_REGISTER(cpu6502, CONFIG_LOG_DEFAULT_LEVEL);

/* ===== CPU 指令跟踪 ===== */
static int  trace_remaining = 0;

void cpu6502_trace_enable(int max_instructions)
{
    trace_remaining = max_instructions;
}

bool cpu6502_trace_is_active(void)
{
    return trace_remaining > 0;
}

/* ===== 前向声明 ===== */
static uint8_t fetch_byte(cpu6502_t *cpu, nes_bus_t *bus);
static uint16_t fetch_word(cpu6502_t *cpu, nes_bus_t *bus);
static void push_byte(cpu6502_t *cpu, nes_bus_t *bus, uint8_t val);
static uint8_t pop_byte(cpu6502_t *cpu, nes_bus_t *bus);
static void push_word(cpu6502_t *cpu, nes_bus_t *bus, uint16_t val);
static uint16_t pop_word(cpu6502_t *cpu, nes_bus_t *bus);

/* 标志位辅助 */
static inline void set_flag(cpu6502_t *cpu, uint8_t flag, bool val) {
    if (val) cpu->p.raw |= flag; else cpu->p.raw &= ~flag;
}

#define SET_Z(cpu, val)  set_flag(cpu, 0x02, (val) == 0)
#define SET_N(cpu, val)  set_flag(cpu, 0x80, (val) & 0x80)

static inline bool is_cross_page(uint16_t a, uint16_t b) {
    return (a & 0xFF00) != (b & 0xFF00);
}

/* ===== 寻址模式函数 ===== */

/* 返回操作数值而非地址 (直接读) */
static uint8_t addr_imm(cpu6502_t *cpu, nes_bus_t *bus, uint8_t *cycles) {
    return fetch_byte(cpu, bus);
}

static uint8_t addr_zp(cpu6502_t *cpu, nes_bus_t *bus, uint8_t *cycles) {
    return fetch_byte(cpu, bus);
}

static uint8_t addr_zpx(cpu6502_t *cpu, nes_bus_t *bus, uint8_t *cycles) {
    uint8_t base = fetch_byte(cpu, bus);
    bus_cpu_read(bus, base); /* 空读 */
    return (base + cpu->x) & 0xFF;
}

static uint8_t addr_zpy(cpu6502_t *cpu, nes_bus_t *bus, uint8_t *cycles) {
    uint8_t base = fetch_byte(cpu, bus);
    bus_cpu_read(bus, base);
    return (base + cpu->y) & 0xFF;
}

/* 返回地址的操作数写入函数 (这些不返回操作数, 而是提供地址写入接口) */
static uint16_t addr_abs(cpu6502_t *cpu, nes_bus_t *bus, uint8_t *cycles) {
    return fetch_word(cpu, bus);
}

static uint16_t addr_abx(cpu6502_t *cpu, nes_bus_t *bus, uint8_t *cycles) {
    uint16_t base = fetch_word(cpu, bus);
    uint16_t eff = base + cpu->x;
    if (is_cross_page(base, eff)) *cycles += 1;
    return eff;
}

static uint16_t addr_aby(cpu6502_t *cpu, nes_bus_t *bus, uint8_t *cycles) {
    uint16_t base = fetch_word(cpu, bus);
    uint16_t eff = base + cpu->y;
    if (is_cross_page(base, eff)) *cycles += 1;
    return eff;
}

static uint16_t addr_ind(cpu6502_t *cpu, nes_bus_t *bus, uint8_t *cycles) {
    uint16_t ptr = fetch_word(cpu, bus);
    /* 6502 间接跳转 bug: 跨页时不递增高位 */
    uint8_t lo = bus_cpu_read(bus, ptr);
    uint8_t hi = bus_cpu_read(bus, (ptr & 0xFF00) | ((ptr + 1) & 0x00FF));
    return (hi << 8) | lo;
}

static uint16_t addr_izx(cpu6502_t *cpu, nes_bus_t *bus, uint8_t *cycles) {
    uint8_t base = fetch_byte(cpu, bus);
    bus_cpu_read(bus, base); /* 空读 */
    uint8_t zp = (base + cpu->x) & 0xFF;
    uint8_t lo = bus_cpu_read(bus, zp);
    uint8_t hi = bus_cpu_read(bus, (zp + 1) & 0xFF);
    return ((uint16_t)hi << 8) | lo;
}

static uint16_t addr_izy(cpu6502_t *cpu, nes_bus_t *bus, uint8_t *cycles) {
    uint8_t base = fetch_byte(cpu, bus);
    uint8_t lo = bus_cpu_read(bus, base);
    uint8_t hi = bus_cpu_read(bus, (base + 1) & 0xFF);
    uint16_t abs = ((uint16_t)hi << 8) | lo;
    uint16_t eff = abs + cpu->y;
    if (is_cross_page(abs, eff)) *cycles += 1;
    return eff;
}

/* ===== 内存访问辅助 ===== */

static uint8_t fetch_byte(cpu6502_t *cpu, nes_bus_t *bus)
{
    return bus_cpu_read(bus, cpu->pc++);
}

static uint16_t fetch_word(cpu6502_t *cpu, nes_bus_t *bus)
{
    uint8_t lo = fetch_byte(cpu, bus);
    uint8_t hi = fetch_byte(cpu, bus);
    return (hi << 8) | lo;
}

static void push_byte(cpu6502_t *cpu, nes_bus_t *bus, uint8_t val)
{
    bus_cpu_write(bus, 0x0100 | cpu->sp, val);
    cpu->sp--;
}

static uint8_t pop_byte(cpu6502_t *cpu, nes_bus_t *bus)
{
    cpu->sp++;
    return bus_cpu_read(bus, 0x0100 | cpu->sp);
}

static void push_word(cpu6502_t *cpu, nes_bus_t *bus, uint16_t val)
{
    push_byte(cpu, bus, val >> 8);
    push_byte(cpu, bus, val & 0xFF);
}

static uint16_t pop_word(cpu6502_t *cpu, nes_bus_t *bus)
{
    uint8_t lo = pop_byte(cpu, bus);
    uint8_t hi = pop_byte(cpu, bus);
    return (hi << 8) | lo;
}

/* ===== NMI/IRQ ===== */

void cpu6502_nmi(cpu6502_t *cpu, nes_bus_t *bus)
{
    push_word(cpu, bus, cpu->pc);
    push_byte(cpu, bus, (cpu->p.raw & ~0x10) | 0x20); /* B=0 U=1 */
    cpu->p.i = 1;
    cpu->pc = bus_cpu_read(bus, 0xFFFA) | (bus_cpu_read(bus, 0xFFFB) << 8);
    cpu->total_cycles += 7;
}

void cpu6502_irq(cpu6502_t *cpu, nes_bus_t *bus)
{
    if (cpu->p.i) return;
    push_word(cpu, bus, cpu->pc);
    push_byte(cpu, bus, (cpu->p.raw & ~0x10) | 0x20); /* B=0 U=1 */
    cpu->p.i = 1;
    cpu->pc = bus_cpu_read(bus, 0xFFFE) | (bus_cpu_read(bus, 0xFFFF) << 8);
    cpu->total_cycles += 7;
}

/* ===== 复位 ===== */

void cpu6502_reset(cpu6502_t *cpu, nes_bus_t *bus)
{
    cpu->a = 0;
    cpu->x = 0;
    cpu->y = 0;
    cpu->sp = 0xFD;
    cpu->p.raw = 0x34; /* I=1, unused=1 */
    cpu->pc = bus_cpu_read(bus, 0xFFFC) | (bus_cpu_read(bus, 0xFFFD) << 8);
    cpu->total_cycles = 7;
}

/* ===== 指令执行 (56条指令) ===== */

/* ADC - Add with Carry */
static void exec_adc(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    uint8_t val = bus_cpu_read(bus, addr);
    uint16_t sum = cpu->a + val + (cpu->p.c ? 1 : 0);
    cpu->p.c = (sum > 0xFF);
    cpu->p.v = ((cpu->a ^ sum) & (val ^ sum) & 0x80) != 0;
    cpu->a = sum & 0xFF;
    SET_Z(cpu, cpu->a); SET_N(cpu, cpu->a);
}

/* AND - Logical AND */
static void exec_and(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    cpu->a &= bus_cpu_read(bus, addr);
    SET_Z(cpu, cpu->a); SET_N(cpu, cpu->a);
}

/* ASL - Arithmetic Shift Left */
static void exec_asl_acc(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    cpu->p.c = (cpu->a & 0x80) != 0;
    cpu->a <<= 1;
    SET_Z(cpu, cpu->a); SET_N(cpu, cpu->a);
}
static void exec_asl(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    uint8_t val = bus_cpu_read(bus, addr);
    cpu->p.c = (val & 0x80) != 0;
    val <<= 1;
    bus_cpu_write(bus, addr, val);
    SET_Z(cpu, val); SET_N(cpu, val);
}

/* Branch instructions - each checks its specific CPU flag.
 * Branch penalty cycles (+1 for taken, +1 for page cross) are handled
 * in cpu6502_step() by comparing PC before/after execution. */
static void exec_bpl(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    (void)bus; (void)mode_type;
    if (!cpu->p.n) cpu->pc += (int8_t)addr;
}
static void exec_bmi(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    (void)bus; (void)mode_type;
    if (cpu->p.n) cpu->pc += (int8_t)addr;
}
static void exec_bvc(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    (void)bus; (void)mode_type;
    if (!cpu->p.v) cpu->pc += (int8_t)addr;
}
static void exec_bvs(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    (void)bus; (void)mode_type;
    if (cpu->p.v) cpu->pc += (int8_t)addr;
}
static void exec_bcc(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    (void)bus; (void)mode_type;
    if (!cpu->p.c) cpu->pc += (int8_t)addr;
}
static void exec_bcs(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    (void)bus; (void)mode_type;
    if (cpu->p.c) cpu->pc += (int8_t)addr;
}
static void exec_bne(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    (void)bus; (void)mode_type;
    if (!cpu->p.z) cpu->pc += (int8_t)addr;
}
static void exec_beq(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    (void)bus; (void)mode_type;
    if (cpu->p.z) cpu->pc += (int8_t)addr;
}

/* BIT - Bit Test */
static void exec_bit(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    uint8_t val = bus_cpu_read(bus, addr);
    SET_Z(cpu, cpu->a & val);
    cpu->p.v = (val & 0x40) != 0;
    cpu->p.n = (val & 0x80) != 0;
}

/* BRK - Break */
static void exec_brk(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    (void)addr; (void)mode_type;
    cpu->pc++;
    push_word(cpu, bus, cpu->pc);
    push_byte(cpu, bus, cpu->p.raw | 0x30); /* BRK: B=1 U=1 */
    cpu->p.i = 1;
    cpu->pc = bus_cpu_read(bus, 0xFFFE) | (bus_cpu_read(bus, 0xFFFF) << 8);
}

/* CLC/SEC/CLI/SEI/CLD/SED/CLV - Flag Control */
static void exec_clc(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) { cpu->p.c = 0; }
static void exec_sec(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) { cpu->p.c = 1; }
static void exec_cli(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) { cpu->p.i = 0; }
static void exec_sei(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) { cpu->p.i = 1; }
static void exec_cld(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) { cpu->p.d = 0; }
static void exec_sed(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) { cpu->p.d = 1; }
static void exec_clv(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) { cpu->p.v = 0; }

/* CMP/CPX/CPY - Compare */
static void exec_cmp(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    uint8_t val = bus_cpu_read(bus, addr);
    uint8_t result = cpu->a - val;
    cpu->p.c = cpu->a >= val;
    SET_Z(cpu, result); SET_N(cpu, result);
}
static void exec_cpx(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    uint8_t val = bus_cpu_read(bus, addr);
    uint8_t result = cpu->x - val;
    cpu->p.c = cpu->x >= val;
    SET_Z(cpu, result); SET_N(cpu, result);
}
static void exec_cpy(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    uint8_t val = bus_cpu_read(bus, addr);
    uint8_t result = cpu->y - val;
    cpu->p.c = cpu->y >= val;
    SET_Z(cpu, result); SET_N(cpu, result);
}

/* DEC/DEX/DEY - Decrement */
static void exec_dec(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    uint8_t val = bus_cpu_read(bus, addr) - 1;
    bus_cpu_write(bus, addr, val);
    SET_Z(cpu, val); SET_N(cpu, val);
}
static void exec_dex(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    cpu->x--; SET_Z(cpu, cpu->x); SET_N(cpu, cpu->x);
}
static void exec_dey(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    cpu->y--; SET_Z(cpu, cpu->y); SET_N(cpu, cpu->y);
}

/* EOR - Exclusive OR */
static void exec_eor(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    cpu->a ^= bus_cpu_read(bus, addr);
    SET_Z(cpu, cpu->a); SET_N(cpu, cpu->a);
}

/* INC/INX/INY - Increment */
static void exec_inc(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    uint8_t val = bus_cpu_read(bus, addr) + 1;
    bus_cpu_write(bus, addr, val);
    SET_Z(cpu, val); SET_N(cpu, val);
}
static void exec_inx(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    cpu->x++; SET_Z(cpu, cpu->x); SET_N(cpu, cpu->x);
}
static void exec_iny(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    cpu->y++; SET_Z(cpu, cpu->y); SET_N(cpu, cpu->y);
}

/* JMP - Jump */
static void exec_jmp_abs(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    cpu->pc = addr;
}

/* JSR - Jump to Subroutine */
static void exec_jsr(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    push_word(cpu, bus, cpu->pc - 1);
    cpu->pc = addr;
}

/* LDA/LDX/LDY - Load */
static void exec_lda(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    cpu->a = bus_cpu_read(bus, addr);
    SET_Z(cpu, cpu->a); SET_N(cpu, cpu->a);
}
static void exec_ldx(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    cpu->x = bus_cpu_read(bus, addr);
    SET_Z(cpu, cpu->x); SET_N(cpu, cpu->x);
}
static void exec_ldy(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    cpu->y = bus_cpu_read(bus, addr);
    SET_Z(cpu, cpu->y); SET_N(cpu, cpu->y);
}

/* LSR - Logical Shift Right */
static void exec_lsr_acc(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    cpu->p.c = cpu->a & 0x01;
    cpu->a >>= 1;
    SET_Z(cpu, cpu->a); SET_N(cpu, cpu->a);
}
static void exec_lsr(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    uint8_t val = bus_cpu_read(bus, addr);
    cpu->p.c = val & 0x01;
    val >>= 1;
    bus_cpu_write(bus, addr, val);
    SET_Z(cpu, val); SET_N(cpu, val);
}

/* NOP - No Operation */
static void exec_nop(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    (void)addr; (void)mode_type;
}

/* ORA - Logical OR */
static void exec_ora(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    cpu->a |= bus_cpu_read(bus, addr);
    SET_Z(cpu, cpu->a); SET_N(cpu, cpu->a);
}

/* PHA/PHP - Push */
static void exec_pha(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    push_byte(cpu, bus, cpu->a);
}
static void exec_php(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    push_byte(cpu, bus, cpu->p.raw | 0x30); /* B=1, unused=1 */
}

/* PLA/PLP - Pull */
static void exec_pla(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    cpu->a = pop_byte(cpu, bus);
    SET_Z(cpu, cpu->a); SET_N(cpu, cpu->a);
}
static void exec_plp(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    cpu->p.raw = (pop_byte(cpu, bus) & 0xEF) | 0x20; /* B=0, unused=1 */
}

/* ROL - Rotate Left */
static void exec_rol_acc(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    uint8_t old_c = cpu->p.c;
    cpu->p.c = (cpu->a & 0x80) != 0;
    cpu->a = (cpu->a << 1) | old_c;
    SET_Z(cpu, cpu->a); SET_N(cpu, cpu->a);
}
static void exec_rol(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    uint8_t val = bus_cpu_read(bus, addr);
    uint8_t old_c = cpu->p.c;
    cpu->p.c = (val & 0x80) != 0;
    val = (val << 1) | old_c;
    bus_cpu_write(bus, addr, val);
    SET_Z(cpu, val); SET_N(cpu, val);
}

/* ROR - Rotate Right */
static void exec_ror_acc(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    uint8_t old_c = cpu->p.c;
    cpu->p.c = cpu->a & 0x01;
    cpu->a = (cpu->a >> 1) | (old_c << 7);
    SET_Z(cpu, cpu->a); SET_N(cpu, cpu->a);
}
static void exec_ror(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    uint8_t val = bus_cpu_read(bus, addr);
    uint8_t old_c = cpu->p.c;
    cpu->p.c = val & 0x01;
    val = (val >> 1) | (old_c << 7);
    bus_cpu_write(bus, addr, val);
    SET_Z(cpu, val); SET_N(cpu, val);
}

/* RTI - Return from Interrupt */
static void exec_rti(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    cpu->p.raw = (pop_byte(cpu, bus) & 0xEF) | 0x20; /* B=0, unused=1 */
    cpu->pc = pop_word(cpu, bus);
}

/* RTS - Return from Subroutine */
static void exec_rts(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    cpu->pc = pop_word(cpu, bus) + 1;
}

/* SBC - Subtract with Carry */
static void exec_sbc(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    uint8_t val = bus_cpu_read(bus, addr) ^ 0xFF;
    uint16_t sum = cpu->a + val + (cpu->p.c ? 1 : 0);
    cpu->p.c = (sum > 0xFF);
    cpu->p.v = ((cpu->a ^ sum) & (val ^ sum) & 0x80) != 0;
    cpu->a = sum & 0xFF;
    SET_Z(cpu, cpu->a); SET_N(cpu, cpu->a);
}

/* STA/STX/STY - Store */
static void exec_sta(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    bus_cpu_write(bus, addr, cpu->a);
}
static void exec_stx(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    bus_cpu_write(bus, addr, cpu->x);
}
static void exec_sty(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    bus_cpu_write(bus, addr, cpu->y);
}

/* TAX/TAY/TXA/TYA/TSX/TXS - Transfer */
static void exec_tax(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    cpu->x = cpu->a; SET_Z(cpu, cpu->x); SET_N(cpu, cpu->x);
}
static void exec_tay(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    cpu->y = cpu->a; SET_Z(cpu, cpu->y); SET_N(cpu, cpu->y);
}
static void exec_txa(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    cpu->a = cpu->x; SET_Z(cpu, cpu->a); SET_N(cpu, cpu->a);
}
static void exec_tya(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    cpu->a = cpu->y; SET_Z(cpu, cpu->a); SET_N(cpu, cpu->a);
}
static void exec_tsx(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    cpu->x = cpu->sp; SET_Z(cpu, cpu->x); SET_N(cpu, cpu->x);
}
static void exec_txs(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    cpu->sp = cpu->x;
}

/* ===== 非官方指令 (Unofficial Opcodes) ===== */

/* LAX: Load A and X from memory (LDA + LDX) */
static void exec_lax(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    (void)mode_type;
    uint8_t val = bus_cpu_read(bus, addr);
    cpu->a = val;
    cpu->x = val;
    SET_Z(cpu, val); SET_N(cpu, val);
}

/* SAX: Store A AND X to memory */
static void exec_sax(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    (void)mode_type;
    bus_cpu_write(bus, addr, cpu->a & cpu->x);
}

/* DCP: DEC memory then CMP with A */
static void exec_dcp(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    (void)mode_type;
    uint8_t val = bus_cpu_read(bus, addr) - 1;
    bus_cpu_write(bus, addr, val);
    uint8_t result = cpu->a - val;
    cpu->p.c = cpu->a >= val;
    SET_Z(cpu, result); SET_N(cpu, result);
}

/* ISB: INC memory then SBC with A */
static void exec_isb(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    (void)mode_type;
    uint8_t val = bus_cpu_read(bus, addr) + 1;
    bus_cpu_write(bus, addr, val);
    uint8_t inv = val ^ 0xFF;
    uint16_t sum = cpu->a + inv + (cpu->p.c ? 1 : 0);
    cpu->p.c = (sum > 0xFF);
    cpu->p.v = ((cpu->a ^ sum) & (inv ^ sum) & 0x80) != 0;
    cpu->a = sum & 0xFF;
    SET_Z(cpu, cpu->a); SET_N(cpu, cpu->a);
}

/* SLO: ASL memory then ORA with A */
static void exec_slo(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    (void)mode_type;
    uint8_t val = bus_cpu_read(bus, addr);
    cpu->p.c = (val & 0x80) != 0;
    val <<= 1;
    bus_cpu_write(bus, addr, val);
    cpu->a |= val;
    SET_Z(cpu, cpu->a); SET_N(cpu, cpu->a);
}

/* RLA: ROL memory then AND with A */
static void exec_rla(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    (void)mode_type;
    uint8_t val = bus_cpu_read(bus, addr);
    uint8_t old_c = cpu->p.c;
    cpu->p.c = (val & 0x80) != 0;
    val = (val << 1) | old_c;
    bus_cpu_write(bus, addr, val);
    cpu->a &= val;
    SET_Z(cpu, cpu->a); SET_N(cpu, cpu->a);
}

/* SRE: LSR memory then EOR with A */
static void exec_sre(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    (void)mode_type;
    uint8_t val = bus_cpu_read(bus, addr);
    cpu->p.c = val & 0x01;
    val >>= 1;
    bus_cpu_write(bus, addr, val);
    cpu->a ^= val;
    SET_Z(cpu, cpu->a); SET_N(cpu, cpu->a);
}

/* RRA: ROR memory then ADC with A */
static void exec_rra(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    (void)mode_type;
    uint8_t val = bus_cpu_read(bus, addr);
    uint8_t old_c = cpu->p.c;
    cpu->p.c = val & 0x01;
    val = (val >> 1) | (old_c << 7);
    bus_cpu_write(bus, addr, val);
    uint16_t sum = cpu->a + val + (cpu->p.c ? 1 : 0);
    cpu->p.c = (sum > 0xFF);
    cpu->p.v = ((cpu->a ^ sum) & (val ^ sum) & 0x80) != 0;
    cpu->a = sum & 0xFF;
    SET_Z(cpu, cpu->a); SET_N(cpu, cpu->a);
}

/* ===== Opcode 表: 每个 opcode 的周期数、寻址模式、执行函数 ===== */

/*
 * 寻址模式编码:
 * 0=IMM, 1=ZP, 2=ZPX, 3=ZPY, 4=ABS, 5=ABX, 6=ABY, 7=IND, 8=IZX, 9=IZY,
 * 10=REL, 11=IMP, 12=ACC
 */

typedef void (*exec_func_t)(cpu6502_t *, nes_bus_t *, uint16_t, uint8_t);

struct opcode_entry {
    uint8_t     cycles;
    uint8_t     mode;
    exec_func_t exec;
};

/* 前向声明所有执行函数 */

static const struct opcode_entry opcode_table[256];

/* ===== cpu6502_step: 执行一条指令 ===== */

static uint32_t step_count=0;
static bool first_step=true;
int cpu6502_step(cpu6502_t *cpu, nes_bus_t *bus)
{
    /* PC 合法性检查: 防止跳转到未映射区域导致总线故障 */
    if (cpu->pc < 0x2000) {
        return 7;
    }

    static uint16_t last_pc=0xFFFF;static uint32_t same_cnt=0;
    if(cpu->pc==last_pc){same_cnt++;}else{same_cnt=0;last_pc=cpu->pc;}
    if(cpu->pc<0x8000&&same_cnt>10){

        cpu->pc=bus_cpu_read(bus,0xFFFC)|(bus_cpu_read(bus,0xFFFD)<<8);
        same_cnt=0;last_pc=cpu->pc;
    }

    step_count++;
    uint8_t opcode=fetch_byte(cpu,bus);
    const struct opcode_entry *entry = &opcode_table[opcode];

    /* 指令跟踪 */
    if(0 && trace_remaining > 0) {
        trace_remaining--;
        LOG_INF("CPU #%d PC=%04X OP=%02X A=%02X X=%02X Y=%02X SP=%02X P=%02X",
                trace_remaining, cpu->pc - 1, opcode,
                cpu->a, cpu->x, cpu->y, cpu->sp, cpu->p.raw);
    }

    uint8_t extra_cycles = 0;
    uint16_t addr = 0;

    /* 解析寻址模式获取操作数地址 */
    switch (entry->mode) {
    case 0:  fetch_byte(cpu, bus); addr = cpu->pc - 1;         break; /* IMM: 指向ROM */
    case 1:  addr = addr_zp(cpu, bus, &extra_cycles);           break; /* ZP */
    case 2:  addr = addr_zpx(cpu, bus, &extra_cycles);          break; /* ZPX */
    case 3:  addr = addr_zpy(cpu, bus, &extra_cycles);          break; /* ZPY */
    case 4:  addr = addr_abs(cpu, bus, &extra_cycles);          break; /* ABS */
    case 5:  addr = addr_abx(cpu, bus, &extra_cycles);          break; /* ABX */
    case 6:  addr = addr_aby(cpu, bus, &extra_cycles);          break; /* ABY */
    case 7:  addr = addr_ind(cpu, bus, &extra_cycles);          break; /* IND */
    case 8:  addr = addr_izx(cpu, bus, &extra_cycles);          break; /* IZX (返回地址) */
    case 9:  addr = addr_izy(cpu, bus, &extra_cycles);          break; /* IZY (返回地址) */
    case 10: { /* REL: branch offset, exec inline for cycle tracking */
        addr = fetch_byte(cpu, bus);
        uint16_t pc_before = cpu->pc;
        entry->exec(cpu, bus, addr, entry->mode);
        if (cpu->pc != pc_before) {
            extra_cycles += 1; /* branch taken */
            if (is_cross_page(pc_before, cpu->pc)) extra_cycles += 1; /* page cross */
        }
        int total_cycles = entry->cycles + extra_cycles;
        cpu->total_cycles += total_cycles;
        return total_cycles;
    }
    case 11: case 12: break; /* IMP/ACC: 不需要地址 */
    default: break;
    }

    /* 执行指令 (非 REL 模式) */
    entry->exec(cpu, bus, addr, entry->mode);

    int total_cycles = entry->cycles + extra_cycles;
    cpu->total_cycles += total_cycles;
    return total_cycles;
}

/* ===== 调试 ===== */

void cpu6502_dump_state(cpu6502_t *cpu, char *buf, size_t buf_size)
{
    snprintf(buf, buf_size,
             "A:%02X X:%02X Y:%02X SP:%02X PC:%04X P:%02X (N:%d V:%d :%d B:%d D:%d I:%d Z:%d C:%d) CYC:%llu",
             cpu->a, cpu->x, cpu->y, cpu->sp, cpu->pc, cpu->p.raw,
             cpu->p.n, cpu->p.v, 1, cpu->p.b, cpu->p.d, cpu->p.i, cpu->p.z, cpu->p.c,
             (unsigned long long)cpu->total_cycles);
}

/* ===== Opcode 表定义 ===== */

/* 宏简化 opcode 定义 */
#define O(c, m, fn) { c, m, (exec_func_t)fn }
#define IMP 11
#define ACC 12
#define IMM 0
#define ZP  1
#define ZPX 2
#define ZPY 3
#define ABS 4
#define ABX 5
#define ABY 6
#define IND 7
#define IZX 8
#define IZY 9
#define REL 10

/* 非法 opcode 统一处理为 NOP */
/* ANC: AND #imm + set Carry to bit7 */
static void exec_anc(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    (void)mode_type; cpu->a &= bus_cpu_read(bus, addr);
    SET_Z(cpu, cpu->a); SET_N(cpu, cpu->a);
    cpu->p.c = (cpu->a & 0x80) != 0;
}
/* ALR: AND #imm + LSR A */
static void exec_alr(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    (void)mode_type; cpu->a &= bus_cpu_read(bus, addr);
    cpu->p.c = cpu->a & 0x01; cpu->a >>= 1;
    SET_Z(cpu, cpu->a); SET_N(cpu, cpu->a);
}
/* ARR: AND #imm + ROR A */
static void exec_arr(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    (void)mode_type; cpu->a &= bus_cpu_read(bus, addr);
    uint8_t old_c = cpu->p.c;
    cpu->p.c = cpu->a & 0x01; cpu->a = (cpu->a >> 1) | (old_c << 7);
    SET_Z(cpu, cpu->a); SET_N(cpu, cpu->a);
}
/* AXS: A AND X - imm -> X (same as SBX) */
static void exec_axs(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    (void)mode_type;
    uint8_t val = (cpu->a & cpu->x) - bus_cpu_read(bus, addr);
    cpu->x = val; cpu->p.c = (cpu->a & cpu->x) >= bus_cpu_read(bus, addr);
    SET_Z(cpu, val); SET_N(cpu, val);
}
/* LAS: LDA/TSX with stack */
static void exec_las(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    (void)mode_type;
    uint8_t val = bus_cpu_read(bus, addr) & cpu->sp;
    cpu->a = val; cpu->x = val; cpu->sp = val;
    SET_Z(cpu, val); SET_N(cpu, val);
}
/* ATX: LDA #imm, TAX (similar to ORA but immediate, unstable) */
static void exec_atx(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    (void)mode_type;
    uint8_t val = bus_cpu_read(bus, addr);
    cpu->a = val; cpu->x = val;
    SET_Z(cpu, val); SET_N(cpu, val);
}
/* SHY: Store Y AND (addr_hi+1) to abs,X */
static void exec_shy(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    (void)mode_type;
    uint8_t val = cpu->y & (uint8_t)((addr >> 8) + 1);
    bus_cpu_write(bus, addr, val);
}
/* SHX: Store X AND (addr_hi+1) to abs,Y */
static void exec_shx(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    (void)mode_type;
    uint8_t val = cpu->x & (uint8_t)((addr >> 8) + 1);
    bus_cpu_write(bus, addr, val);
}
/* SHA: Store A AND X AND (addr_hi+1) to abs,Y */
static void exec_sha(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    (void)mode_type;
    uint8_t val = cpu->a & cpu->x & (uint8_t)((addr >> 8) + 1);
    bus_cpu_write(bus, addr, val);
}
static void exec_ill(cpu6502_t *cpu, nes_bus_t *bus, uint16_t addr, uint8_t mode_type) {
    static uint32_t ill_count=0;
    if(++ill_count<=5) LOG_ERR("ILLEGAL OP at PC=%04X addr=%04X", cpu->pc-1, addr);
}

static const struct opcode_entry opcode_table[256] = {
/*  0x00 */ O(7, IMP, exec_brk),  O(6, IZX, exec_ora),  O(2, IMP, exec_ill),  O(8, IZX, exec_slo),
/*  0x04 */ O(3, ZP,  exec_nop),  O(3, ZP,  exec_ora),  O(5, ZP,  exec_asl),  O(5, ZP,  exec_slo),
/*  0x08 */ O(3, IMP, exec_php),  O(2, IMM, exec_ora),  O(2, ACC, exec_asl_acc), O(2, IMM, exec_anc),
/*  0x0C */ O(4, ABS, exec_nop),  O(4, ABS, exec_ora),  O(6, ABS, exec_asl),  O(6, ABS, exec_slo),

/*  0x10 */ O(2, REL, exec_bpl),  O(5, IZY, exec_ora),  O(2, IMP, exec_ill),  O(8, IZY, exec_slo),
/*  0x14 */ O(4, ZPX, exec_nop),  O(4, ZPX, exec_ora),  O(6, ZPX, exec_asl),  O(6, ZPX, exec_slo),
/*  0x18 */ O(2, IMP, exec_clc),  O(4, ABY, exec_ora),  O(2, IMP, exec_nop),  O(7, ABY, exec_slo),
/*  0x1C */ O(4, ABX, exec_nop),  O(4, ABX, exec_ora),  O(7, ABX, exec_asl),  O(7, ABX, exec_slo),

/*  0x20 */ O(6, ABS, exec_jsr),  O(6, IZX, exec_and),  O(2, IMP, exec_ill),  O(8, IZX, exec_rla),
/*  0x24 */ O(3, ZP,  exec_bit),  O(3, ZP,  exec_and),  O(5, ZP,  exec_rol),  O(5, ZP,  exec_rla),
/*  0x28 */ O(4, IMP, exec_plp),  O(2, IMM, exec_and),  O(2, ACC, exec_rol_acc), O(2, IMM, exec_anc),
/*  0x2C */ O(4, ABS, exec_bit),  O(4, ABS, exec_and),  O(6, ABS, exec_rol),  O(6, ABS, exec_rla),

/*  0x30 */ O(2, REL, exec_bmi),  O(5, IZY, exec_and),  O(2, IMP, exec_ill),  O(8, IZY, exec_rla),
/*  0x34 */ O(4, ZPX, exec_nop),  O(4, ZPX, exec_and),  O(6, ZPX, exec_rol),  O(6, ZPX, exec_rla),
/*  0x38 */ O(2, IMP, exec_sec),  O(4, ABY, exec_and),  O(2, IMP, exec_nop),  O(7, ABY, exec_rla),
/*  0x3C */ O(4, ABX, exec_nop),  O(4, ABX, exec_and),  O(7, ABX, exec_rol),  O(7, ABX, exec_rla),

/*  0x40 */ O(6, IMP, exec_rti),  O(6, IZX, exec_eor),  O(2, IMP, exec_ill),  O(8, IZX, exec_sre),
/*  0x44 */ O(3, ZP,  exec_nop),  O(3, ZP,  exec_eor),  O(5, ZP,  exec_lsr),  O(5, ZP,  exec_sre),
/*  0x48 */ O(3, IMP, exec_pha),  O(2, IMM, exec_eor),  O(2, ACC, exec_lsr_acc), O(2, IMM, exec_alr),
/*  0x4C */ O(3, ABS, exec_jmp_abs), O(4, ABS, exec_eor), O(6, ABS, exec_lsr), O(6, ABS, exec_sre),

/*  0x50 */ O(2, REL, exec_bvc),  O(5, IZY, exec_eor),  O(2, IMP, exec_ill),  O(8, IZY, exec_sre),
/*  0x54 */ O(4, ZPX, exec_nop),  O(4, ZPX, exec_eor),  O(6, ZPX, exec_lsr),  O(6, ZPX, exec_sre),
/*  0x58 */ O(2, IMP, exec_cli),  O(4, ABY, exec_eor),  O(2, IMP, exec_nop),  O(7, ABY, exec_sre),
/*  0x5C */ O(4, ABX, exec_nop),  O(4, ABX, exec_eor),  O(7, ABX, exec_lsr),  O(7, ABX, exec_sre),

/*  0x60 */ O(6, IMP, exec_rts),  O(6, IZX, exec_adc),  O(2, IMP, exec_ill),  O(8, IZX, exec_rra),
/*  0x64 */ O(3, ZP,  exec_nop),  O(3, ZP,  exec_adc),  O(5, ZP,  exec_ror),  O(5, ZP,  exec_rra),
/*  0x68 */ O(4, IMP, exec_pla),  O(2, IMM, exec_adc),  O(2, ACC, exec_ror_acc), O(2, IMM, exec_arr),
/*  0x6C */ O(5, IND, exec_jmp_abs), O(4, ABS, exec_adc), O(6, ABS, exec_ror), O(6, ABS, exec_rra),

/*  0x70 */ O(2, REL, exec_bvs),  O(5, IZY, exec_adc),  O(2, IMP, exec_ill),  O(8, IZY, exec_rra),
/*  0x74 */ O(4, ZPX, exec_nop),  O(4, ZPX, exec_adc),  O(6, ZPX, exec_ror),  O(6, ZPX, exec_rra),
/*  0x78 */ O(2, IMP, exec_sei),  O(4, ABY, exec_adc),  O(2, IMP, exec_nop),  O(7, ABY, exec_rra),
/*  0x7C */ O(4, ABX, exec_nop),  O(4, ABX, exec_adc),  O(7, ABX, exec_ror),  O(7, ABX, exec_rra),

/*  0x80 */ O(2, IMM, exec_nop),  O(6, IZX, exec_sta),  O(2, IMM, exec_nop),  O(6, IZX, exec_sax),
/*  0x84 */ O(3, ZP,  exec_sty),  O(3, ZP,  exec_sta),  O(3, ZP,  exec_stx),  O(3, ZP,  exec_sax),
/*  0x88 */ O(2, IMP, exec_dey),  O(2, IMM, exec_nop),  O(2, IMP, exec_txa),  O(2, IMM, exec_nop),
/*  0x8C */ O(4, ABS, exec_sty),  O(4, ABS, exec_sta),  O(4, ABS, exec_stx),  O(4, ABS, exec_sax),

/*  0x90 */ O(2, REL, exec_bcc),  O(6, IZY, exec_sta),  O(2, IMP, exec_ill),  O(6, IZY, exec_ill),
/*  0x94 */ O(4, ZPX, exec_sty),  O(4, ZPX, exec_sta),  O(4, ZPY, exec_stx),  O(4, ZPY, exec_sax),
/*  0x98 */ O(2, IMP, exec_tya),  O(5, ABY, exec_sta),  O(2, IMP, exec_txs),  O(5, ABY, exec_ill),
/*  0x9C */ O(5, ABX, exec_shy),  O(5, ABX, exec_sta),  O(5, ABY, exec_shx),  O(5, ABY, exec_sha),

/*  0xA0 */ O(2, IMM, exec_ldy),  O(6, IZX, exec_lda),  O(2, IMM, exec_ldx),  O(6, IZX, exec_lax),
/*  0xA4 */ O(3, ZP,  exec_ldy),  O(3, ZP,  exec_lda),  O(3, ZP,  exec_ldx),  O(3, ZP,  exec_lax),
/*  0xA8 */ O(2, IMP, exec_tay),  O(2, IMM, exec_lda),  O(2, IMP, exec_tax),  O(2, IMM, exec_atx),
/*  0xAC */ O(4, ABS, exec_ldy),  O(4, ABS, exec_lda),  O(4, ABS, exec_ldx),  O(4, ABS, exec_lax),

/*  0xB0 */ O(2, REL, exec_bcs),  O(5, IZY, exec_lda),  O(2, IMP, exec_ill),  O(5, IZY, exec_lax),
/*  0xB4 */ O(4, ZPX, exec_ldy),  O(4, ZPX, exec_lda),  O(4, ZPY, exec_ldx),  O(4, ZPY, exec_lax),
/*  0xB8 */ O(2, IMP, exec_clv),  O(4, ABY, exec_lda),  O(2, IMP, exec_tsx),  O(4, ABY, exec_las),
/*  0xBC */ O(4, ABX, exec_ldy),  O(4, ABX, exec_lda),  O(4, ABY, exec_ldx),  O(4, ABY, exec_lax),

/*  0xC0 */ O(2, IMM, exec_cpy),  O(6, IZX, exec_cmp),  O(2, IMM, exec_nop),  O(8, IZX, exec_dcp),
/*  0xC4 */ O(3, ZP,  exec_cpy),  O(3, ZP,  exec_cmp),  O(5, ZP,  exec_dec),  O(5, ZP,  exec_dcp),
/*  0xC8 */ O(2, IMP, exec_iny),  O(2, IMM, exec_cmp),  O(2, IMP, exec_dex),  O(2, IMM, exec_axs),
/*  0xCC */ O(4, ABS, exec_cpy),  O(4, ABS, exec_cmp),  O(6, ABS, exec_dec),  O(6, ABS, exec_dcp),

/*  0xD0 */ O(2, REL, exec_bne),  O(5, IZY, exec_cmp),  O(2, IMP, exec_ill),  O(8, IZY, exec_dcp),
/*  0xD4 */ O(4, ZPX, exec_nop),  O(4, ZPX, exec_cmp),  O(6, ZPX, exec_dec),  O(6, ZPX, exec_dcp),
/*  0xD8 */ O(2, IMP, exec_cld),  O(4, ABY, exec_cmp),  O(2, IMP, exec_nop),  O(7, ABY, exec_dcp),
/*  0xDC */ O(4, ABX, exec_nop),  O(4, ABX, exec_cmp),  O(7, ABX, exec_dec),  O(7, ABX, exec_dcp),

/*  0xE0 */ O(2, IMM, exec_cpx),  O(6, IZX, exec_sbc),  O(2, IMM, exec_nop),  O(8, IZX, exec_isb),
/*  0xE4 */ O(3, ZP,  exec_cpx),  O(3, ZP,  exec_sbc),  O(5, ZP,  exec_inc),  O(5, ZP,  exec_isb),
/*  0xE8 */ O(2, IMP, exec_inx),  O(2, IMM, exec_sbc),  O(2, IMP, exec_nop),  O(2, IMM, /*SBC_IMM*/exec_sbc),
/*  0xEC */ O(4, ABS, exec_cpx),  O(4, ABS, exec_sbc),  O(6, ABS, exec_inc),  O(6, ABS, exec_isb),

/*  0xF0 */ O(2, REL, exec_beq),  O(5, IZY, exec_sbc),  O(2, IMP, exec_ill),  O(8, IZY, exec_isb),
/*  0xF4 */ O(4, ZPX, exec_nop),  O(4, ZPX, exec_sbc),  O(6, ZPX, exec_inc),  O(6, ZPX, exec_isb),
/*  0xF8 */ O(2, IMP, exec_sed),  O(4, ABY, exec_sbc),  O(2, IMP, exec_nop),  O(7, ABY, exec_isb),
/*  0xFC */ O(4, ABX, exec_nop),  O(4, ABX, exec_sbc),  O(7, ABX, exec_inc),  O(7, ABX, exec_isb),
};
