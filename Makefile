# ============================================================================
#  doomOS — Makefile
#
#  Builds a bootable hybrid ISO (UEFI + Legacy BIOS) that boots directly
#  into DOOM on bare metal.
#
#  Targets:
#    all      — Build doomOS.iso (default)
#    clean    — Remove build artifacts
#    deps     — Fetch doomgeneric, Limine, and the shareware WAD
# ============================================================================

# ── Toolchain ───────────────────────────────────────────────────────────────

CC      = gcc
LD      = ld
NASM    = nasm
OBJCOPY = objcopy

CFLAGS  = -Wall -Wextra -std=gnu11 -O2 \
          -ffreestanding -fno-stack-protector -fno-stack-check \
          -fno-pie -fno-PIC -m64 -march=x86-64 \
          -mno-red-zone -mcmodel=kernel

# doomgeneric needs relaxed warnings for third-party code
DOOM_CFLAGS = $(CFLAGS) \
              -Wno-unused-parameter -Wno-sign-compare \
              -Wno-missing-field-initializers

LDFLAGS = -nostdlib -static -T linker.ld -z max-page-size=0x200000

# ── Paths ───────────────────────────────────────────────────────────────────

DOOMGENERIC_DIR = doomgeneric
DOOMGENERIC_SRC = $(DOOMGENERIC_DIR)/doomgeneric
LIMINE_DIR      = limine
WAD_FILE        = DOOM1.WAD

# Limine binary release URL (v8.x branch)
LIMINE_BRANCH   = v8.x-binary

# Shareware DOOM1.WAD URL (legal shareware distribution)
WAD_URL = https://distro.ibiblio.org/slitaz/sources/packages/d/doom1.wad

# ── Source files ────────────────────────────────────────────────────────────

KERNEL_SRC = src/kernel.c src/keyboard.c

# Collect doomgeneric .c sources (exclude platform-specific files)
DOOM_EXCLUDE = $(DOOMGENERIC_SRC)/doomgeneric_xlib.c \
               $(DOOMGENERIC_SRC)/doomgeneric_sdl.c  \
               $(DOOMGENERIC_SRC)/doomgeneric_win.c  \
               $(DOOMGENERIC_SRC)/doomgeneric_soso.c \
               $(DOOMGENERIC_SRC)/doomgeneric_sosox.c \
               $(DOOMGENERIC_SRC)/doomgeneric_null.c \
               $(DOOMGENERIC_SRC)/doomgeneric_allegro.c \
               $(DOOMGENERIC_SRC)/doomgeneric_emscripten.c \
               $(DOOMGENERIC_SRC)/doomgeneric_linuxvt.c \
               $(DOOMGENERIC_SRC)/i_sound.c \
               $(DOOMGENERIC_SRC)/i_sdlsound.c \
               $(DOOMGENERIC_SRC)/i_sdlmusic.c \
               $(DOOMGENERIC_SRC)/i_allegrosound.c \
               $(DOOMGENERIC_SRC)/i_allegromusic.c \
               $(DOOMGENERIC_SRC)/i_cdmus.c

DOOM_SRCS = $(filter-out $(DOOM_EXCLUDE), $(wildcard $(DOOMGENERIC_SRC)/*.c))

# Object files
KERNEL_OBJ = $(KERNEL_SRC:.c=.o)
DOOM_OBJ   = $(DOOM_SRCS:.c=.o)
WAD_OBJ    = doom1_wad.o

ALL_OBJ    = $(KERNEL_OBJ) $(DOOM_OBJ) $(WAD_OBJ)

# ── Primary Targets ────────────────────────────────────────────────────────

.PHONY: all clean deps

all: doomOS.iso

# ── Dependencies ────────────────────────────────────────────────────────────

deps: $(DOOMGENERIC_SRC)/doomgeneric.h $(LIMINE_DIR)/limine $(WAD_FILE)

$(DOOMGENERIC_SRC)/doomgeneric.h:
	@echo "==> Cloning doomgeneric..."
	git clone --depth 1 https://github.com/ozkl/doomgeneric.git $(DOOMGENERIC_DIR)

$(LIMINE_DIR)/limine:
	@echo "==> Cloning Limine ($(LIMINE_BRANCH))..."
	git clone --depth 1 --branch $(LIMINE_BRANCH) https://github.com/limine-bootloader/limine.git $(LIMINE_DIR)
	$(MAKE) -C $(LIMINE_DIR)

$(WAD_FILE):
	@echo "==> Downloading shareware DOOM1.WAD..."
	curl -L -o $(WAD_FILE) "$(WAD_URL)"

# ── Compile Rules ───────────────────────────────────────────────────────────

# Kernel sources (include limine headers + doomgeneric headers + our stubs)
src/%.o: src/%.c $(DOOMGENERIC_SRC)/doomgeneric.h
	$(CC) $(CFLAGS) -isystem include -I$(LIMINE_DIR) -I$(DOOMGENERIC_SRC) -c $< -o $@

# doomgeneric sources (use DOOM_CFLAGS with x87 FPU and stub headers)
$(DOOMGENERIC_SRC)/%.o: $(DOOMGENERIC_SRC)/%.c
	$(CC) $(DOOM_CFLAGS) -isystem include -I$(DOOMGENERIC_SRC) -c $< -o $@

# Embed WAD into an object file placed in the .wad section
$(WAD_OBJ): $(WAD_FILE)
	$(OBJCOPY) -I binary -O elf64-x86-64 \
	    --rename-section .data=.wad,alloc,load,readonly,data \
	    $(WAD_FILE) $(WAD_OBJ)

# ── Link ────────────────────────────────────────────────────────────────────

doomOS.elf: $(ALL_OBJ)
	$(LD) $(LDFLAGS) -o $@ $(ALL_OBJ)

# ── ISO Construction ────────────────────────────────────────────────────────

doomOS.iso: doomOS.elf $(LIMINE_DIR)/limine
	@echo "==> Building hybrid ISO..."
	rm -rf iso_root
	mkdir -p iso_root/boot/limine iso_root/EFI/BOOT

	cp doomOS.elf              iso_root/boot/doomOS.elf
	cp limine.conf             iso_root/boot/limine/

	# Copy Limine BIOS files
	cp $(LIMINE_DIR)/limine-bios.sys    iso_root/boot/limine/
	cp $(LIMINE_DIR)/limine-bios-cd.bin iso_root/boot/limine/

	# Copy Limine UEFI CD image
	cp $(LIMINE_DIR)/limine-uefi-cd.bin iso_root/boot/limine/

	# Copy Limine UEFI boot files
	cp $(LIMINE_DIR)/BOOTX64.EFI  iso_root/EFI/BOOT/
	cp $(LIMINE_DIR)/BOOTIA32.EFI iso_root/EFI/BOOT/ 2>/dev/null || true

	# Create the ISO with xorriso
	xorriso -as mkisofs \
	    -b boot/limine/limine-bios-cd.bin \
	    -no-emul-boot -boot-load-size 4 -boot-info-table \
	    --efi-boot boot/limine/limine-uefi-cd.bin \
	    -efi-boot-part --efi-boot-image \
	    --protective-msdos-label \
	    iso_root -o $@

	# Install Limine BIOS stages onto the ISO
	$(LIMINE_DIR)/limine bios-install $@

	@echo "==> doomOS.iso ready!"

# ── Cleanup ─────────────────────────────────────────────────────────────────

clean:
	rm -f $(KERNEL_OBJ) $(WAD_OBJ) doomOS.elf doomOS.iso
	rm -f $(DOOM_OBJ)
	rm -rf iso_root
