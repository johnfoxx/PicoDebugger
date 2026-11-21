# La chaîne GNU ARM suffit pour les ELF. picotool sert à produire l'UF2.
CC_ARM ?= arm-none-eabi-gcc
PICOTOOL ?= picotool
ARCH = -mcpu=cortex-m33 -mthumb -mfloat-abi=soft
CFLAGS_ARM = $(ARCH) -std=c11 -Og -g3 -Wall -Wextra -Werror \
	-ffreestanding -fno-builtin -fno-unwind-tables -fno-asynchronous-unwind-tables -Iinclude
LDFLAGS_ARM = -nostdlib -Wl,--build-id=none

.PHONY: all flash uf2
all: build/blink_sram.elf
flash: build/blink_flash.elf
uf2: build/blink_flash.uf2

# Le script fixe l'adresse où la sonde déposera l'ELF.
build/blink_sram.elf: main.c sram.ld include/rp2350_regs.h
	mkdir -p build
	$(CC_ARM) $(CFLAGS_ARM) $(LDFLAGS_ARM) -T sram.ld \
		-Wl,-Map,build/blink_sram.map main.c -o $@

# Inclure les métadonnées de boot et lier les valeurs initiales de .data en Flash.
build/blink_flash.elf: main.c image_def.c flash.ld include/rp2350_regs.h
	mkdir -p build
	$(CC_ARM) $(CFLAGS_ARM) $(LDFLAGS_ARM) -T flash.ld \
		-Wl,-Map,build/blink_flash.map main.c image_def.c -o $@

# L'outil sur le PC emballe l'ELF pour le mode BOOTSEL de la carte.
build/blink_flash.uf2: build/blink_flash.elf
	$(PICOTOOL) uf2 convert $< $@ --family rp2350-arm-s
