SHELL := /bin/bash

CROSS_COMPILE ?= aarch64-nextui-linux-gnu-
CC := $(CROSS_COMPILE)gcc
STRIP := $(CROSS_COMPILE)strip
SRC := $(shell find /workspace/src -name '*.c' -print | sort)
APOSTROPHE_HEADER := /workspace/third_party/apostrophe/include/apostrophe.h
INCLUDES := -I/workspace/third_party/apostrophe/include -I/workspace/src
PKG_CONFIG := pkg-config
SDL_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2)
SDL_LDFLAGS := $(shell $(PKG_CONFIG) --libs sdl2) -lSDL2_ttf -lSDL2_image

CFLAGS := -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter \
	-mcpu=cortex-a53 -mtune=cortex-a53 \
	-DPLATFORM_TG5040 \
	-ffunction-sections -fdata-sections -fstack-protector-strong \
	$(INCLUDES) $(SDL_CFLAGS)

LDFLAGS := $(SDL_LDFLAGS) -Wl,--gc-sections -lpthread -lm

all: $(BUILD_DIR)/joes-calibrage

$(BUILD_DIR)/joes-calibrage: $(SRC) $(APOSTROPHE_HEADER)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)
	$(STRIP) $@
