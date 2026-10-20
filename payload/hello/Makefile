# hello.elf, built with the PS5 payload SDK (tools/setup.sh installs it).
#
#   make                          build hello.elf
#   make send PS5_HOST=<PS5 IP>   send it to an ELF loader on port 9021
#   make clean

PS5_PAYLOAD_SDK ?= $(HOME)/.ps5-starter/ps5-payload-sdk
PS5_HOST ?=
PS5_PORT ?= 9021

ifneq ($(wildcard $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk),)
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else ifneq ($(filter-out clean,$(or $(MAKECMDGOALS),all)),)
    $(error No payload SDK at $(PS5_PAYLOAD_SDK): run tools/setup.sh first)
endif

ELF := hello.elf
CFLAGS := -Wall -Wextra -O2

all: $(ELF)

$(ELF): hello.c
	$(CC) $(CFLAGS) -o $@ $^

send: $(ELF)
	@test -n "$(PS5_HOST)" || { echo "usage: make send PS5_HOST=<PS5 IP>"; exit 2; }
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $(ELF)

clean:
	rm -f $(ELF)

.PHONY: all send clean
