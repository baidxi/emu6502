/*
 * NES Emulator - 6502 CPU Simulator
 */

#ifndef CPU_6502_H
#define CPU_6502_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "bus.h"

/* 6502 CPU 寄存器 */
typedef struct {
    uint8_t  a;       /* 累加器 */
    uint8_t  x;       /* X 索引寄存器 */
    uint8_t  y;       /* Y 索引寄存器 */
    uint8_t  sp;      /* 栈指针 (0x0100-0x01FF) */
    uint16_t pc;      /* 程序计数器 */
    union {
        uint8_t raw;
        struct {
            uint8_t c : 1;  /* Carry */
            uint8_t z : 1;  /* Zero */
            uint8_t i : 1;  /* Interrupt Disable */
            uint8_t d : 1;  /* Decimal (NES 中不用) */
            uint8_t b : 1;  /* Break */
            uint8_t : 1;    /* 未使用 */
            uint8_t v : 1;  /* Overflow */
            uint8_t n : 1;  /* Negative */
        };
    } p;               /* 状态寄存器 */
    uint64_t total_cycles; /* 累计执行周期数 */
} cpu6502_t;

/* CPU 生命周期 */
void cpu6502_reset(cpu6502_t *cpu, struct nes_bus *bus);
void cpu6502_nmi(cpu6502_t *cpu, struct nes_bus *bus);
void cpu6502_irq(cpu6502_t *cpu, struct nes_bus *bus);

/* 执行一条指令, 返回消耗的 CPU 周期数 */
int cpu6502_step(cpu6502_t *cpu, struct nes_bus *bus);

/* 获取 CPU 状态字符串 (调试用) */
void cpu6502_dump_state(cpu6502_t *cpu, char *buf, size_t buf_size);

/* 启用/禁用 CPU 指令跟踪 (调试用, 会大量输出日志) */
void cpu6502_trace_enable(int max_instructions);
bool cpu6502_trace_is_active(void);

#endif /* CPU_6502_H */
