SHELL := /bin/bash

APP_NAME := joes-calibrage
PAK_NAME := Joe's Calibrage
APOSTROPHE_DIR := third_party/apostrophe
APOSTROPHE_BRANCH := main
BUILD_DIR := build
DIST_DIR := $(BUILD_DIR)/release
STAGING_DIR := $(BUILD_DIR)/staging
CACHE_DIR := .cache
TEST_BUILD_DIR := $(BUILD_DIR)/tests
TEST_BIN := $(TEST_BUILD_DIR)/calibrage_tests
SRC_FILES := $(shell find src -name '*.c' -print | sort)
TEST_SRC_FILES := tests/calibrage_tests.c src/calibration.c src/config.c src/raw_input.c
MY355_TOOLCHAIN := ghcr.io/loveretro/my355-toolchain:latest
ADB ?= adb

COMMON_INCLUDES := -I$(APOSTROPHE_DIR)/include -Isrc

.PHONY: all native mac run-mac run-native test-native my355 package-my355 package \
	deploy clean help update-apostrophe setup-nextui-preview-cache clean-nextui-preview-cache

native: mac
run-native: run-mac
all: my355

$(APOSTROPHE_DIR)/include/apostrophe.h:
	git submodule update --init

update-apostrophe: $(APOSTROPHE_DIR)/include/apostrophe.h
	@set -euo pipefail; \
	git -C "$(APOSTROPHE_DIR)" fetch origin "$(APOSTROPHE_BRANCH)"; \
	commit=$$(git -C "$(APOSTROPHE_DIR)" rev-parse "origin/$(APOSTROPHE_BRANCH)"); \
	git -C "$(APOSTROPHE_DIR)" checkout "$$commit" >/dev/null; \
	echo "Apostrophe pinned to $$commit"

setup-nextui-preview-cache: $(APOSTROPHE_DIR)/include/apostrophe.h
	@$(MAKE) -C $(APOSTROPHE_DIR) setup-nextui-preview-cache \
		CACHE_DIR=$(CURDIR)/$(CACHE_DIR)

clean-nextui-preview-cache:
	rm -rf $(CACHE_DIR)/nextui-preview

mac: $(APOSTROPHE_DIR)/include/apostrophe.h
	@$(MAKE) setup-nextui-preview-cache
	@mkdir -p $(BUILD_DIR)/mac
	cc -std=gnu11 -O0 -g \
		-DPLATFORM_MAC \
		$(COMMON_INCLUDES) \
		$(shell pkg-config --cflags sdl2 SDL2_ttf SDL2_image) \
		-o $(BUILD_DIR)/mac/$(APP_NAME) \
		$(SRC_FILES) \
		$(shell pkg-config --libs sdl2 SDL2_ttf SDL2_image) \
		-lm -lpthread

run-mac: mac
	./$(BUILD_DIR)/mac/$(APP_NAME)

$(TEST_BIN): $(TEST_SRC_FILES)
	@mkdir -p $(TEST_BUILD_DIR)
	cc -std=gnu11 -O0 -g \
		-DPLATFORM_MAC -DTESTING \
		-Isrc \
		-o $(TEST_BIN) \
		$(TEST_SRC_FILES)

test-native: $(TEST_BIN)
	./$(TEST_BIN)

my355: $(APOSTROPHE_DIR)/include/apostrophe.h
	@mkdir -p $(BUILD_DIR)/my355
	docker run --rm \
		-v "$(CURDIR)":/workspace \
		$(MY355_TOOLCHAIN) \
		make -C /workspace -f ports/my355/Makefile BUILD_DIR=/workspace/$(BUILD_DIR)/my355

package-my355: my355
	@rm -rf "$(BUILD_DIR)/my355/$(PAK_NAME).pak"
	@mkdir -p "$(BUILD_DIR)/my355/$(PAK_NAME).pak"
	@cp "$(BUILD_DIR)/my355/$(APP_NAME)" "$(BUILD_DIR)/my355/$(PAK_NAME).pak/"
	@cp launch.sh pak.json "$(BUILD_DIR)/my355/$(PAK_NAME).pak/"
	@if [ -f LICENSE ]; then cp LICENSE "$(BUILD_DIR)/my355/$(PAK_NAME).pak/"; fi
	@mkdir -p "$(DIST_DIR)/my355"
	@rm -f "$(DIST_DIR)/my355/$(PAK_NAME).pak.zip"
	@cd "$(BUILD_DIR)/my355" && zip -r "$(CURDIR)/$(DIST_DIR)/my355/$(PAK_NAME).pak.zip" "$(PAK_NAME).pak" -x '.*'

package: package-my355
	@rm -rf "$(STAGING_DIR)"
	@mkdir -p "$(STAGING_DIR)/Tools/my355"
	@cp -a "$(BUILD_DIR)/my355/$(PAK_NAME).pak" "$(STAGING_DIR)/Tools/my355/"
	@mkdir -p "$(DIST_DIR)/all"
	@rm -f "$(DIST_DIR)/all/$(PAK_NAME).pakz"
	@cd "$(STAGING_DIR)" && zip -9 -r "$(CURDIR)/$(DIST_DIR)/all/$(PAK_NAME).pakz" . -x '.*'

deploy: package-my355
	@SERIAL="$(ADB_SERIAL)"; \
	if [ -z "$$SERIAL" ]; then \
		SERIAL=$$($(ADB) devices | awk 'NR>1 && $$2=="device" {print $$1; exit}'); \
	fi; \
	if [ -z "$$SERIAL" ]; then \
		echo "Error: No online adb device found."; \
		exit 1; \
	fi; \
	TOOLS_ROOT="/mnt/SDCARD/Tools/my355"; \
	TOOLS_DIR="$$TOOLS_ROOT/$(PAK_NAME).pak"; \
	echo "Deploying $(PAK_NAME).pak to $$TOOLS_DIR on $$SERIAL..."; \
	$(ADB) -s "$$SERIAL" shell "rm -rf \"$$TOOLS_DIR\" && mkdir -p \"$$TOOLS_ROOT\""; \
	$(ADB) -s "$$SERIAL" push "$(BUILD_DIR)/my355/$(PAK_NAME).pak" "$$TOOLS_ROOT/"

clean:
	rm -rf $(BUILD_DIR)

help:
	@echo "Targets:"
	@echo "  make test-native   Build and run native unit tests"
	@echo "  make mac           Build the mac development binary"
	@echo "  make run-mac       Build and run the mac binary"
	@echo "  make my355         Cross-build the my355 binary"
	@echo "  make package-my355 Build a my355 .pak.zip"
	@echo "  make package       Build the my355 .pakz"
	@echo "  make deploy        Build and deploy to adb /mnt/SDCARD/Tools/my355"
