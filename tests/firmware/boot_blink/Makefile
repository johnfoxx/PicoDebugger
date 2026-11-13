# Run make here, or make -C tests/firmware/boot_blink from the repository root.
BOARD ?= pico2
HALF_PERIOD_MS ?= 500
JOBS ?= 2
CMAKE ?= cmake
PYTHON ?= python3
PICODBG_CLI ?= ../../../build-gui/pico-debugger-cli
DEBUG_BUILD = build/debugger-$(BOARD)
BOARD_BUILD = build/board-$(BOARD)
CHECK_BUILD = build/check-$(BOARD)

.PHONY: all debugger board check host-test help
all: debugger

debugger:
	$(CMAKE) -S . -B $(DEBUG_BUILD) -DBOOT_BLINK_MODE=debugger -DPICO_BOARD=$(BOARD) -DBOOT_BLINK_HALF_PERIOD_MS=$(HALF_PERIOD_MS)
	$(CMAKE) --build $(DEBUG_BUILD) --parallel $(JOBS)

board:
	$(CMAKE) -S . -B $(BOARD_BUILD) -DBOOT_BLINK_MODE=board -DPICO_BOARD=$(BOARD) -DBOOT_BLINK_HALF_PERIOD_MS=$(HALF_PERIOD_MS)
	$(CMAKE) --build $(BOARD_BUILD) --parallel $(JOBS)

# A separate fast fixture avoids changing the period of the user's build.
check:
	$(CMAKE) -S . -B $(CHECK_BUILD) -DBOOT_BLINK_MODE=debugger -DPICO_BOARD=$(BOARD) -DBOOT_BLINK_HALF_PERIOD_MS=1
	$(CMAKE) --build $(CHECK_BUILD) --parallel $(JOBS)
	$(PYTHON) tests/check_debugger.py --cli "$(PICODBG_CLI)" --elf $(CHECK_BUILD)/boot_blink.elf --board $(BOARD)

host-test:
	$(CMAKE) -S tests -B build/host
	$(CMAKE) --build build/host --parallel $(JOBS)
	ctest --test-dir build/host --output-on-failure

help:
	@echo "make [debugger|board|check|host-test] BOARD=pico2 HALF_PERIOD_MS=500"
	@echo "BOARD: pico or pico2. board requires PICO_SDK_PATH; debugger/check require arm-none-eabi-gcc."
	@echo "check uses a separate 1 ms fixture and PICODBG_CLI; host-test needs only a host C compiler."
