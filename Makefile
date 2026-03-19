# Makefile für ESP32 PlatformIO Template

PLATFORMIO ?= pio
BOARD ?= esp32cam
PYTHON ?= python3

# Optionales "Argument" nach flash/monitor/run (z. B. "make flash 1")
ACTION_TARGETS := flash monitor run deploy-fs
ifneq ($(filter $(ACTION_TARGETS),$(firstword $(MAKECMDGOALS))),)
  ARG := $(word 2,$(MAKECMDGOALS))
  ifneq ($(ARG),)
    NR := $(ARG)
    # Dummy-Ziel erzeugen, damit die Zahl (z. B. "1") kein echtes Target ist
    $(eval $(ARG):;@:)
  endif
endif

# Optionale Flags je nach NR
ifdef NR
  UPLOAD_FLAG := --upload-port /dev/ttyACM$(NR)
  MONITOR_FLAG := --port /dev/ttyACM$(NR)
else
  UPLOAD_FLAG :=
  MONITOR_FLAG :=
endif

.PHONY: all build flash monitor run clean list deploy-web deploy-fs deploy-flash web-headers setup-config help

# Default target
all: build

# Generate include/config.h from template if it does not exist yet
setup-config:
	@$(PYTHON) tools/setup-config.py

# Help target
help:
	@echo "ESP32 PlatformIO Template - Makefile Commands"
	@echo "=============================================="
	@echo ""
	@echo "Building:"
	@echo "  make build              Build firmware (includes web-headers)"
	@echo "  make web-headers        Build web UI and generate C headers"
	@echo "  make clean              Clean build artifacts"
	@echo ""
	@echo "Flashing:"
	@echo "  make flash              Flash firmware (auto-detect port)"
	@echo "  make flash 1            Flash to /dev/ttyACM1"
	@echo "  make deploy-fs          Upload filesystem (SPIFFS/LittleFS)"
	@echo "  make deploy-fs 2        Upload filesystem to /dev/ttyACM2"
	@echo ""
	@echo "Monitoring:"
	@echo "  make monitor            Serial monitor (auto-detect port)"
	@echo "  make monitor 1          Serial monitor on /dev/ttyACM1"
	@echo ""
	@echo "Combined:"
	@echo "  make run                Build + Flash + Monitor"
	@echo "  make run 1              Build + Flash + Monitor on /dev/ttyACM1"
	@echo "  make deploy-flash       Web + Firmware + Filesystem (complete)"
	@echo ""
	@echo "Web Deployment:"
	@echo "  make deploy-web         Deploy web to data-template/"
	@echo ""
	@echo "Tools:"
	@echo "  make list               List connected ESP32 devices"
	@echo ""
	@echo "Release:"
	@echo "  make release VERSION=v1.0.0   Create tagged release"
	@echo ""
	@echo "Board Selection:"
	@echo "  BOARD=esp32   (default)  ESP32 Generic"
	@echo "  BOARD=esp32c3            ESP32-C3"
	@echo "  BOARD=esp32s3            ESP32-S3"
	@echo "  BOARD=esp32c6            ESP32-C6"
	@echo ""
	@echo "Examples:"
	@echo "  make build BOARD=esp32c3"
	@echo "  make flash 1 BOARD=esp32c3"
	@echo "  make run 2 BOARD=esp32s3"

# Build firmware (includes web headers)
build: setup-config web-headers
	@echo "🔨 Building firmware for $(BOARD)..."
	$(PLATFORMIO) run --environment $(BOARD)
	@echo "✅ Build complete"

# Build web interface and generate C headers
web-headers:
	@echo "🌐 Building web UI (Vite)..."
	@cd web && npm install --silent
	@cd web && npm run build
	@echo "🗜️  Converting web files to gzipped C headers..."
	@$(PYTHON) tools/web-to-header.py web/dist -o lib/WebService
	@echo "✅ Headers generated in lib/WebService/web_files.h"

# Flash firmware
# make flash        -> ohne --upload-port (auto-detect)
# make flash 1      -> Upload auf /dev/ttyACM1
flash:
	@echo "📤 Flashing firmware to $(BOARD)..."
	$(PLATFORMIO) run --target upload --environment $(BOARD) $(UPLOAD_FLAG)
	@echo "✅ Firmware flashed"

# Serial monitor
# make monitor      -> ohne --port (auto-detect)
# make monitor 2    -> Monitor auf /dev/ttyACM2
monitor:
	@echo "📟 Starting serial monitor for $(BOARD)..."
	$(PLATFORMIO) device monitor --environment $(BOARD) $(MONITOR_FLAG)

# Build, flash, and monitor
# make run          -> flash danach monitor (ohne Port)
# make run 1        -> flash/monitor auf /dev/ttyACM1
run: build flash monitor

# Clean build artifacts
clean:
	@echo "🧹 Cleaning build artifacts..."
	$(PLATFORMIO) run --target clean --environment $(BOARD)
	@cd web && rm -rf dist node_modules
	@rm -f lib/WebService/web_files.h
	@echo "✅ Clean complete"

# Web Interface Deployment Targets
# =================================

# Deploy web interface from /web/dist/ to /data-template/
deploy-web:
	@echo "🚀 Deploying web interface to data-template/..."
	@./tools/deploy-web.sh
	@echo "✅ Web deployment complete"

# Upload filesystem (data-template/) to ESP32
deploy-fs:
	@echo "📁 Uploading filesystem to ESP32..."
	$(PLATFORMIO) run --target uploadfs --environment $(BOARD) $(UPLOAD_FLAG)
	@echo "✅ Filesystem uploaded"

# Deploy web interface and flash ESP32 with firmware + filesystem
deploy-flash: deploy-web build flash deploy-fs
	@echo ""
	@echo "🎉 Complete deployment finished!"
	@echo "✅ Web interface deployed to data-template/"
	@echo "✅ Firmware flashed"
	@echo "✅ Filesystem uploaded"

# List connected ESP32 devices
# make list         -> nur ESP-Geräte auf /dev/ttyACM<N> mit Nummern (ohne Duplikate)
list:
	@echo "Connected ESP32 Devices:"
	@echo "NR  PORT          DESCRIPTION"
	@echo "--- ------------- --------------------------------------------------"
	@$(PLATFORMIO) device list --json-output 2>/dev/null | jq -r 'map(select(((.hwid // "") | test("VID:PID=303A:|VID:PID=10C4:|VID:PID=1A86:", "i")) or ((.description // "") | test("Espressif|USB JTAG/serial|CP210|CH340", "i")))) | map(select(.port | test("^/dev/tty(ACM|USB)[0-9]+"))) | unique_by(.port) | .[] | (if (.port | test("ACM")) then (.port | capture("ACM(?<n>[0-9]+)").n) else (.port | capture("USB(?<n>[0-9]+)").n) end) + "   " + .port + "  " + (.description // "")' || echo "No devices found or jq not installed"

# Release Management
# ==================

.PHONY: release
release:
	@if [ -z "$(VERSION)" ]; then echo "❌ VERSION required (e.g. make release VERSION=v1.0.0)"; exit 1; fi
	@echo "🚀 Starting automated release $(VERSION)..."
	@echo ""
	@echo "📝 Step 1/6: Updating version files..."
	@echo "$(VERSION)" > VERSION
	@cd web && npm version --no-git-tag-version --allow-same-version ${VERSION#v} --silent
	@echo "✅ Version files updated"
	@echo ""
	@echo "🌐 Step 2/6: Building web interface with new version..."
	@$(MAKE) web-headers
	@echo "✅ Web interface built and embedded"
	@echo ""
	@echo "🔨 Step 3/6: Building firmware..."
	@$(MAKE) build BOARD=$(BOARD)
	@echo "✅ Firmware built"
	@echo ""
	@echo "📦 Step 4/6: Committing release..."
	@git add VERSION web/package.json web/package-lock.json || true
	@git add -f lib/WebService/web_files.h || true
	@git commit -m "Release $(VERSION)" || (echo "⚠️  No changes to commit"; true)
	@echo "✅ Release committed"
	@echo ""
	@echo "🏷️  Step 5/6: Creating and pushing tag..."
	@if git rev-parse $(VERSION) >/dev/null 2>&1; then \
		echo "⚠️  Tag $(VERSION) already exists, deleting old tag..."; \
		git tag -d $(VERSION); \
		git push origin :refs/tags/$(VERSION) 2>/dev/null || true; \
	fi
	@git tag -a $(VERSION) -m "Release $(VERSION)"
	@echo "✅ Tag $(VERSION) created"
	@echo ""
	@echo "🚀 Step 6/6: Pushing to remote..."
	@BRANCH=$$(git rev-parse --abbrev-ref HEAD); \
	echo "📤 Pushing branch: $$BRANCH"; \
	git push origin $$BRANCH; \
	echo "📤 Pushing tag: $(VERSION)"; \
	git push origin $(VERSION); \
	REPO=$$(git config --get remote.origin.url | sed 's/.*github.com[:\/]\(.*\)\.git/\1/' || echo "unknown"); \
	echo ""; \
	echo "✅ ✅ ✅ Release $(VERSION) completed! ✅ ✅ ✅"; \
	echo ""; \
	if [ "$$REPO" != "unknown" ]; then \
		echo "🔗 GitHub: https://github.com/$$REPO"; \
		echo "🔗 Releases: https://github.com/$$REPO/releases"; \
	fi; \
	echo ""; \
	echo "📦 Version: $(VERSION)"; \
	echo "🎯 Board: $(BOARD)"
