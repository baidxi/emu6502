# emu6502 — NES 模拟器

基于 Zephyr RTOS 和 LVGL 的便携式 NES (FC) 模拟器。NES 核心模拟与平台解耦，可移植到任意满足资源要求的 Zephyr 开发板。

## 当前测试平台

| 项目 | 说明 |
|------|------|
| SoC | Allwinner V3s (ARM Cortex-A7 @ 1.2 GHz) |
| RAM | 64 MB DDR2 |
| 显示 | 800×600 RGB LCD |
| 触摸 | 电阻/电容触屏 |
| 存储 | microSD 卡 (FatFS) |
| 开发板 | Sipeed Lichee Zero |

> V3s / Lichee Zero 仅为当前开发调试使用的硬件。模拟核心不依赖特定平台，更换 Zephyr 支持的其他板卡只需适配 `src/platform/` 层。

## 特性

- **6502 CPU 模拟**: 56 条官方指令 + 8 条关键非官方指令 (LAX/SAX/DCP/ISB/SLO/RLA/SRE/RRA)
- **PPU 图形渲染**: 背景 tile + 精灵渲染、sprite-0 命中检测、NES 64 色调色板、2x 像素完美缩放
- **Mapper 0 (NROM)**: 支持 NROM-128 / NROM-256
- **LVGL UI**: 三栏式布局
  - 左侧：十字方向键 (D-pad)
  - 中间：Canvas (256×240 NES 画面 ×2 缩放)
  - 右侧：A/B + Select/Start 动作按钮
- **文件浏览器**: 存储介质的 .nes ROM 文件浏览与加载
- **调试信息栏**: 实时显示 FPS、Mapper 编号、PRG/CHR 大小、帧计数
- **SRAM 存档**: 电池备份游戏的存档持久化

## 快速开始

```bash
# 以 lichee_zero (V3s) 为目标构建
west build -b lichee_zero emu6502 -d build/emu6502

# 更换目标平台 (示例)
# west build -b <your-board> emu6502 -d build/emu6502
```

烧录方式取决于目标板卡，当前 V3s 支持 FEL 模式或 SD 卡启动。

## 项目结构

```
emu6502/
├── CMakeLists.txt
├── prj.conf                  # Kconfig 配置
├── app.overlay               # DTS overlay (SD 卡等)
├── boards/
│   └── lichee_zero.conf      # 板级覆盖 (可按需替换)
└── src/
    ├── main.c                # 入口: 状态机 + 双线程
    ├── nes/                  # NES 模拟核心 (平台无关)
    │   ├── cpu_6502.c/h      # 6502 CPU 模拟器
    │   ├── ppu.c/h           # PPU 图形处理器
    │   ├── bus.c/h           # 内存总线 + IO 映射
    │   ├── cartridge.c/h     # iNES ROM 加载 + Mapper
    │   ├── controller.c/h    # NES 手柄寄存器
    │   └── nes_system.c/h    # CPU/PPU 时序同步
    ├── ui/                   # LVGL 界面 (平台无关)
    │   ├── ui_common.h       # 状态机 + 线程消息定义
    │   ├── ui_filebrowser.c/h
    │   └── ui_game.c/h
    └── platform/             # 平台适配层 (按需替换)
        └── sd_card.c/h       # 存储读写封装
```

## 架构

```
┌──────────────────────────────────────────────┐
│                 LVGL UI 层 (平台无关)          │
│  ┌───────────┐  ┌──────────┐  ┌───────────┐  │
│  │ 文件浏览器 │  │ NES Canvas│  │ 虚拟手柄   │  │
│  └───────────┘  └──────────┘  └───────────┘  │
├──────────────────────────────────────────────┤
│              NES 模拟核心 (平台无关)            │
│  ┌──────┐  ┌─────┐  ┌──────┐  ┌──────────┐  │
│  │ CPU  │  │ PPU │  │ Bus  │  │ Cartridge│  │
│  │ 6502 │  │     │  │      │  │ (Mapper) │  │
│  └──────┘  └─────┘  └──────┘  └──────────┘  │
├──────────────────────────────────────────────┤
│               Zephyr 平台层                    │
│  ┌──────────┐  ┌──────────┐  ┌───────────┐  │
│  │ 存储驱动  │  │ 显示驱动  │  │ 输入驱动   │  │
│  │ (FatFS)  │  │ (LVGL)   │  │ (触屏)    │  │
│  └──────────┘  └──────────┘  └───────────┘  │
└──────────────────────────────────────────────┘
```

### 线程模型

| 线程 | 职责 |
|------|------|
| **主线程** | LVGL 事件处理 + `lv_timer_handler` 循环 |
| **模拟线程** | NES 帧执行 (CPU + PPU 同步)，`k_msgq` 收命令，`k_sem` 通知帧完成 |
| **LVGL Workqueue** | 后台 `lv_timer_handler` (5ms) |

### 状态机

```
MENU_INIT → FILE_BROWSER → LOADING → RUNNING ⇄ PAUSED
                 ↑            ↑          │
                 └────────────┘          │
                  (ROM 加载失败)    [返回]┘
```

## 指令集覆盖率

| 类别 | 数量 | 说明 |
|------|------|------|
| 官方指令 | 56 条 (151 opcode) | 全实现 |
| 非官方指令 | 8 条 (~45 opcode) | LAX SAX DCP ISB SLO RLA SRE RRA |
| KIL/HALT | 12 opcode | 按 NOP 处理 (2 周期) |
| 不稳定指令 | ~20 opcode | 按 NOP 处理 |

## 移植指南

更换目标平台只需：

1. 在 `boards/` 下添加新板卡的 `.conf` 覆盖文件
2. 替换 `src/platform/sd_card.c` 中的存储访问实现
3. 调整 `prj.conf` 中与显示/输入相关的 Kconfig
4. `src/nes/` 和 `src/ui/` 无需修改

## 当前进度

| Phase | 内容 | 状态 |
|-------|------|------|
| Phase 1 | 项目骨架 + 存储 + 文件浏览器 | ✅ |
| Phase 2 | 6502 CPU 全部指令 | ✅ |
| Phase 3 | PPU + 总线 + ROM + Mapper 0 + 系统整合 | ✅ |
| Phase 4 | UI 整合 (Canvas + 虚拟手柄 + 状态机) | ✅ |
| Phase 5 | Mapper 1/2/3/4 + 性能优化 + APU 音频 | ❌ |

## 设计文档

[plans/nes-emulator-design.md](../plans/nes-emulator-design.md)

## License

Apache 2.0
