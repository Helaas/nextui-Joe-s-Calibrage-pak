SHELL := /bin/bash

APP_NAME := joes-calibrage
PAK_NAME := Joe's Calibrage
APOSTROPHE_DIR := third_party/apostrophe
APOSTROPHE_BRANCH := main
PATCH_DIR := patches
BUILD_DIR := build
DIST_DIR := $(BUILD_DIR)/release
STAGING_DIR := $(BUILD_DIR)/staging
CACHE_DIR := .cache
TEST_BUILD_DIR := $(BUILD_DIR)/tests
TEST_BIN := $(TEST_BUILD_DIR)/calibrage_tests
SRC_FILES := $(shell find src -name '*.c' -print | sort)
TEST_SRC_FILES := tests/calibrage_tests.c src/calibration.c src/config.c src/platform.c src/raw_input.c
MY355_TOOLCHAIN := ghcr.io/loveretro/my355-toolchain:latest
TG5040_TOOLCHAIN := ghcr.io/loveretro/tg5040-toolchain:latest
ADB ?= adb

COMMON_INCLUDES := -I$(APOSTROPHE_DIR)/include -Isrc
APOSTROPHE_PATCHES := $(wildcard $(PATCH_DIR)/*.patch)
APOSTROPHE_PATCH_STAMP := $(BUILD_DIR)/apostrophe-patches.stamp

.PHONY: all native mac run-mac run-native test-native my355 tg5040 \
	package-my355 package-tg5040 package do-package deploy deploy-platform \
	clean help update-apostrophe setup-nextui-preview-cache clean-nextui-preview-cache

native: mac
run-native: run-mac
all: my355 tg5040

$(APOSTROPHE_DIR)/include/apostrophe.h:
	git submodule update --init

$(APOSTROPHE_PATCH_STAMP): $(APOSTROPHE_DIR)/include/apostrophe.h $(APOSTROPHE_PATCHES)
	@set -euo pipefail; \
	mkdir -p "$(dir $@)"; \
	cd "$(APOSTROPHE_DIR)"; \
	for patch in $(abspath $(APOSTROPHE_PATCHES)); do \
		if git apply --reverse --check "$$patch" >/dev/null 2>&1; then \
			echo "Apostrophe patch already applied: $$(basename "$$patch")"; \
		else \
			echo "Applying Apostrophe patch: $$(basename "$$patch")"; \
			git apply "$$patch"; \
		fi; \
	done; \
	touch "$(CURDIR)/$@"

update-apostrophe: $(APOSTROPHE_DIR)/include/apostrophe.h
	@set -euo pipefail; \
	for patch in $(abspath $(APOSTROPHE_PATCHES)); do \
		if git -C "$(APOSTROPHE_DIR)" apply --reverse --check "$$patch" >/dev/null 2>&1; then \
			echo "Reversing Apostrophe patch: $$(basename "$$patch")"; \
			git -C "$(APOSTROPHE_DIR)" apply --reverse "$$patch"; \
		fi; \
	done; \
	git -C "$(APOSTROPHE_DIR)" fetch origin "$(APOSTROPHE_BRANCH)"; \
	commit=$$(git -C "$(APOSTROPHE_DIR)" rev-parse "origin/$(APOSTROPHE_BRANCH)"); \
	git -C "$(APOSTROPHE_DIR)" checkout "$$commit" >/dev/null; \
	rm -f "$(APOSTROPHE_PATCH_STAMP)"; \
	echo "Apostrophe pinned to $$commit"

setup-nextui-preview-cache: $(APOSTROPHE_PATCH_STAMP)
	@$(MAKE) -C $(APOSTROPHE_DIR) setup-nextui-preview-cache \
		CACHE_DIR=$(CURDIR)/$(CACHE_DIR)

clean-nextui-preview-cache:
	rm -rf $(CACHE_DIR)/nextui-preview

mac: $(APOSTROPHE_PATCH_STAMP)
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

my355: $(APOSTROPHE_PATCH_STAMP)
	@mkdir -p $(BUILD_DIR)/my355
	docker run --rm \
		-v "$(CURDIR)":/workspace \
		$(MY355_TOOLCHAIN) \
		make -C /workspace -f ports/my355/Makefile BUILD_DIR=/workspace/$(BUILD_DIR)/my355

tg5040: $(APOSTROPHE_PATCH_STAMP)
	@mkdir -p $(BUILD_DIR)/tg5040
	docker run --rm \
		-v "$(CURDIR)":/workspace \
		$(TG5040_TOOLCHAIN) \
		make -C /workspace -f ports/tg5040/Makefile BUILD_DIR=/workspace/$(BUILD_DIR)/tg5040

package-my355: my355
	@$(MAKE) do-package PLATFORM=my355

package-tg5040: tg5040
	@$(MAKE) do-package PLATFORM=tg5040

do-package:
	@rm -rf "$(BUILD_DIR)/$(PLATFORM)/$(PAK_NAME).pak"
	@mkdir -p "$(BUILD_DIR)/$(PLATFORM)/$(PAK_NAME).pak"
	@cp "$(BUILD_DIR)/$(PLATFORM)/$(APP_NAME)" "$(BUILD_DIR)/$(PLATFORM)/$(PAK_NAME).pak/"
	@cp launch.sh pak.json "$(BUILD_DIR)/$(PLATFORM)/$(PAK_NAME).pak/"
	@if [ -f LICENSE ]; then cp LICENSE "$(BUILD_DIR)/$(PLATFORM)/$(PAK_NAME).pak/"; fi
	@mkdir -p "$(DIST_DIR)/$(PLATFORM)"
	@rm -f "$(DIST_DIR)/$(PLATFORM)/$(PAK_NAME).pak.zip"
	@cd "$(BUILD_DIR)/$(PLATFORM)" && zip -r "$(CURDIR)/$(DIST_DIR)/$(PLATFORM)/$(PAK_NAME).pak.zip" "$(PAK_NAME).pak" -x '.*'

package: package-my355 package-tg5040
	@rm -rf "$(STAGING_DIR)"
	@mkdir -p "$(STAGING_DIR)/Tools/my355" "$(STAGING_DIR)/Tools/tg5040"
	@cp -a "$(BUILD_DIR)/my355/$(PAK_NAME).pak" "$(STAGING_DIR)/Tools/my355/"
	@cp -a "$(BUILD_DIR)/tg5040/$(PAK_NAME).pak" "$(STAGING_DIR)/Tools/tg5040/"
	@mkdir -p "$(DIST_DIR)/all"
	@rm -f "$(DIST_DIR)/all/$(PAK_NAME).pakz"
	@cd "$(STAGING_DIR)" && zip -9 -r "$(CURDIR)/$(DIST_DIR)/all/$(PAK_NAME).pakz" . -x '.*'

deploy:
	@echo "Detecting platform..."
	@SERIAL="$(ADB_SERIAL)"; \
	if [ -z "$$SERIAL" ]; then \
		SERIAL=$$($(ADB) devices | awk 'NR>1 && $$2=="device" {print $$1; exit}'); \
	fi; \
	if [ -z "$$SERIAL" ]; then \
		echo "Error: No online adb device found."; \
		exit 1; \
	fi; \
	ADB_CMD="$(ADB) -s $$SERIAL"; \
	FINGERPRINT=$$($$ADB_CMD shell ' \
		cat /proc/device-tree/compatible 2>/dev/null; \
		echo; \
		cat /proc/device-tree/model 2>/dev/null; \
		echo; \
		uname -a 2>/dev/null' 2>/dev/null | tr '\000' '\n' | tr -d '\r'); \
	case "$$FINGERPRINT" in \
		*rk3566*|*miyoo-355*) PLATFORM=my355 ;; \
		*allwinner,a133*|*sun50iw*) PLATFORM=tg5040 ;; \
		*) \
			echo "Error: Could not detect a supported platform from adb fingerprint."; \
			echo "  Serial: $$SERIAL"; \
			echo "  Fingerprint snippet: $$(printf '%s' "$$FINGERPRINT" | head -c 240)"; \
			exit 1; \
			;; \
	esac; \
	echo "Detected adb serial: $$SERIAL"; \
	echo "Detected platform: $$PLATFORM"; \
	$(MAKE) deploy-platform PLATFORM=$$PLATFORM SERIAL=$$SERIAL

deploy-platform:
	@if [ -z "$(PLATFORM)" ] || [ -z "$(SERIAL)" ]; then \
		echo "Error: deploy-platform requires PLATFORM and SERIAL."; \
		exit 1; \
	fi
	@$(MAKE) package-$(PLATFORM)
	@ADB_CMD="$(ADB) -s $(SERIAL)"; \
	TOOLS_ROOT="/mnt/SDCARD/Tools/$(PLATFORM)"; \
	TOOLS_DIR="$$TOOLS_ROOT/$(PAK_NAME).pak"; \
	echo "Deploying $(PAK_NAME).pak to $$TOOLS_DIR on $(SERIAL)..."; \
	$$ADB_CMD shell "rm -rf \"$$TOOLS_DIR\" && mkdir -p \"$$TOOLS_ROOT\""; \
	$$ADB_CMD push "$(BUILD_DIR)/$(PLATFORM)/$(PAK_NAME).pak" "$$TOOLS_ROOT/"

clean:
	rm -rf $(BUILD_DIR)

help:
	@echo "Targets:"
	@echo "  make test-native   Build and run native unit tests"
	@echo "  make mac           Build the mac development binary"
	@echo "  make run-mac       Build and run the mac binary"
	@echo "  make my355         Cross-build the my355 binary"
	@echo "  make tg5040        Cross-build the tg5040 binary"
	@echo "  make package-my355 Build a my355 .pak.zip"
	@echo "  make package-tg5040 Build a tg5040 .pak.zip"
	@echo "  make package       Build the multi-platform .pakz"
	@echo "  make deploy        Detect adb platform, build, and deploy to Tools"
