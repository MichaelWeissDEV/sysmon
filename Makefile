.PHONY: all build debug test run compact once clean install docs help

BUILD_DIR ?= build

all: build

build:
	@cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	@cmake --build $(BUILD_DIR) -j$$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

debug:
	@cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug
	@cmake --build $(BUILD_DIR) -j$$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

test: build
	@cd $(BUILD_DIR) && ctest --output-on-failure

run: build
	@./$(BUILD_DIR)/sysmon

compact: build
	@./$(BUILD_DIR)/sysmon --compact

once: build
	@./$(BUILD_DIR)/sysmon --once

install: build
	@sudo cmake --install $(BUILD_DIR)

docs: build
	@cmake --build $(BUILD_DIR) --target doxygen 2>/dev/null || true
	@if command -v sphinx-build >/dev/null 2>&1; then \
		sphinx-build docs/source docs/_build/html; \
		echo "Sphinx documentation generated at: docs/_build/html/index.html"; \
	fi

clean:
	@rm -rf $(BUILD_DIR)

help:
	@echo "sysmon build targets:"
	@echo "  make         - Build Release binary in ./build/sysmon"
	@echo "  make run     - Build and run live TUI dashboard"
	@echo "  make compact - Run in compact summary mode"
	@echo "  make once    - Print snapshot once to terminal"
	@echo "  make test    - Run GoogleTest test suite"
	@echo "  make docs    - Build Doxygen and Sphinx documentation"
	@echo "  make install - Install to /usr/local/bin"
	@echo "  make clean   - Remove build artifacts"
