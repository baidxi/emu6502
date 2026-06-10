/*
 * NES Emulator - Controller Input
 */

#include "controller.h"

void ctrl_init(nes_ctrl_t *ctrl)
{
    ctrl->port1.buttons   = 0;
    ctrl->port1.shift_reg = 0;
    ctrl->port1.strobe    = false;

    ctrl->port2.buttons   = 0;
    ctrl->port2.shift_reg = 0;
    ctrl->port2.strobe    = false;
}

void ctrl_write_strobe(nes_ctrl_t *ctrl, uint8_t value)
{
    bool new_strobe = (value & 0x01) != 0;

    /* strobe 下降沿: 锁存当前 buttons 到 shift_reg */
    if (ctrl->port1.strobe && !new_strobe) {
        ctrl->port1.shift_reg = ctrl->port1.buttons;
        ctrl->port2.shift_reg = ctrl->port2.buttons;
    }

    /* strobe 高电平期间: 持续重载 shift_reg */
    if (new_strobe) {
        ctrl->port1.shift_reg = ctrl->port1.buttons;
        ctrl->port2.shift_reg = ctrl->port2.buttons;
    }

    ctrl->port1.strobe = new_strobe;
    ctrl->port2.strobe = new_strobe;
}

uint8_t ctrl_read_port1(nes_ctrl_t *ctrl)
{
    uint8_t ret;

    if (ctrl->port1.strobe) {
        /* strobe=1: 返回当前 A 按钮状态, 不移位 */
        ret = ctrl->port1.buttons & BTN_A;
    } else {
        /* strobe=0: 返回 shift_reg bit0, 然后右移, 高位补1 */
        ret = ctrl->port1.shift_reg & 0x01;
        ctrl->port1.shift_reg = 0x80 | (ctrl->port1.shift_reg >> 1);
    }

    return ret | 0x40; /* 标准手柄: bit6 和 bit7 为1 */
}

uint8_t ctrl_read_port2(nes_ctrl_t *ctrl)
{
    uint8_t ret;

    if (ctrl->port2.strobe) {
        ret = ctrl->port2.buttons & BTN_A;
    } else {
        ret = ctrl->port2.shift_reg & 0x01;
        ctrl->port2.shift_reg = 0x80 | (ctrl->port2.shift_reg >> 1);
    }

    return ret | 0x40;
}

void ctrl_set_buttons(nes_controller_t *port, uint8_t mask)
{
    __atomic_fetch_or(&port->buttons, mask, __ATOMIC_RELAXED);
}

void ctrl_clear_buttons(nes_controller_t *port, uint8_t mask)
{
    __atomic_fetch_and(&port->buttons, ~mask, __ATOMIC_RELAXED);
}
