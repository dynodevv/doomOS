# doomOS

A bare-metal unikernel that boots directly into DOOM on x86_64 hardware.

## Overview

doomOS is a minimal 64-bit higher-half kernel that:

- Boots via the **Limine** bootloader (UEFI + Legacy BIOS hybrid)
- Runs the **doomgeneric** DOOM engine with no traditional OS underneath
- Embeds the shareware `DOOM1.WAD` directly into the kernel binary — no filesystem needed
- Uses a PS/2 keyboard driver (Scancode Set 1) for input
- Renders to the Limine linear framebuffer with nearest-neighbour scaling

## Building

### Prerequisites

- `gcc`, `ld`, `objcopy` (GNU Binutils)
- `nasm`
- `make`
- `xorriso`, `mtools`
- `curl`, `git`

### Quick Start

```bash
# Fetch all dependencies (doomgeneric, Limine, DOOM1.WAD shareware)
make deps

# Build the bootable ISO
make all
```

The resulting `doomOS.iso` can be booted in QEMU, VirtualBox, or on real hardware:

```bash
qemu-system-x86_64 -cdrom doomOS.iso -m 256M
```

### CI/CD

Every push triggers a GitHub Actions workflow that:

1. Installs build dependencies on Ubuntu
2. Clones doomgeneric and Limine, downloads the shareware WAD
3. Compiles and links the kernel
4. Builds the hybrid ISO
5. Uploads `doomOS.iso` as a build artifact

## Architecture

| Component        | Details                                                  |
|------------------|----------------------------------------------------------|
| **Kernel**       | 64-bit higher-half, freestanding C, bump allocator       |
| **Bootloader**   | Limine v8.x (UEFI + Legacy BIOS)                        |
| **Video**        | Linear framebuffer (32bpp ARGB), nearest-neighbour scale |
| **Input**        | PS/2 port 0x60/0x64, Scancode Set 1 → Doom key codes    |
| **WAD loading**  | `objcopy` embeds WAD into `.wad` ELF section; read from RAM |
| **Game engine**  | [doomgeneric](https://github.com/ozkl/doomgeneric)      |

## Controls

| Key           | Action        |
|---------------|---------------|
| Arrow keys    | Move / turn   |
| Ctrl          | Fire          |
| Space         | Use / open    |
| Shift         | Run           |
| 1–9           | Weapon select |
| Escape        | Menu          |
| Enter         | Confirm       |
| Tab           | Automap       |

## File Structure

```
├── .github/workflows/build.yml   # CI/CD pipeline
├── src/
│   ├── kernel.c                   # Entry point, allocator, Doom callbacks
│   ├── keyboard.c                 # PS/2 keyboard driver
│   └── keyboard.h                 # Scancode definitions
├── limine.conf                    # Boot menu configuration
├── linker.ld                      # Higher-half linker script
├── Makefile                       # Master build script
└── README.md                      # This file
```

## License

This project is provided as-is. DOOM1.WAD (shareware) is © id Software.
The doomgeneric engine is licensed under the GNU GPL v2.
