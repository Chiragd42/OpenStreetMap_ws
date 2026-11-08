.PHONY: help release run basicTest serve frontend test

BUILD_DIR ?= build-release
BINARY := $(BUILD_DIR)/osm_sheet1
PORT ?= 8080
FRONTEND_PORT ?= 5500

help:
	@echo "Required Sheet 1 commands:"
	@echo "  make release"
	@echo "  make run PBF=<file.pbf>"
	@echo "  make basicTest PBF=<file.pbf> INPUT=<input_file> OUTPUT=<output_file>"
	@echo "Additional commands: make serve, make frontend, make test"

release:
	cmake -S . -B "$(BUILD_DIR)" -DCMAKE_BUILD_TYPE=Release
	cmake --build "$(BUILD_DIR)" --parallel

run: release
	@test -n "$(PBF)" || { echo "PBF is required: make run PBF=<file.pbf>"; exit 2; }
	@test -f "$(PBF)" || { echo "PBF file not found: $(PBF)"; exit 2; }
	"$(BINARY)" --pbf "$(PBF)"

basicTest: release
	@test -n "$(PBF)" || { echo "PBF is required"; exit 2; }
	@test -n "$(INPUT)" || { echo "INPUT is required"; exit 2; }
	@test -n "$(OUTPUT)" || { echo "OUTPUT is required"; exit 2; }
	@test -f "$(PBF)" || { echo "PBF file not found: $(PBF)"; exit 2; }
	@test -f "$(INPUT)" || { echo "Input file not found: $(INPUT)"; exit 2; }
	@mkdir -p "$(dir $(OUTPUT))"
	"$(BINARY)" --pbf "$(PBF)" --basicTest "$(INPUT)" "$(OUTPUT)"

serve: release
	@test -n "$(PBF)" || { echo "PBF is required: make serve PBF=<file.pbf>"; exit 2; }
	@test -f "$(PBF)" || { echo "PBF file not found: $(PBF)"; exit 2; }
	"$(BINARY)" --pbf "$(PBF)" --serve --port "$(PORT)"

frontend:
	python3 -m http.server "$(FRONTEND_PORT)" --bind 127.0.0.1 --directory frontend

test: release
	ctest --test-dir "$(BUILD_DIR)" --output-on-failure
