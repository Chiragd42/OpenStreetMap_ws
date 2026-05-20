.PHONY: prep server ui up

BUILD_DIR := build-release
BINARY := $(BUILD_DIR)/osm_geocoder
PBF ?= data/pbf/baden-wuerttemberg-260518.osm.pbf
CACHE ?= data/cache/baden-wuerttemberg-260518.bin
PORT ?= 8080
UI_PORT ?= 5500

prep:
	@start_ts=$$(date +%s); \
	log_file=/tmp/osm_prep.log; \
	: > $$log_file; \
	echo "[1/3] Setting up environment (cmake + build)..."; \
	if ! cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release >>$$log_file 2>&1; then \
		echo "prep failed: cmake configure error"; \
		tail -n 40 $$log_file; \
		exit 1; \
	fi; \
	if ! cmake --build $(BUILD_DIR) -j >>$$log_file 2>&1; then \
		echo "prep failed: build error"; \
		tail -n 40 $$log_file; \
		exit 1; \
	fi; \
	if ! mkdir -p $$(dirname $(CACHE)); then \
		echo "prep failed: could not create cache directory"; \
		exit 1; \
	fi; \
	if [ ! -f "$(PBF)" ]; then \
		echo "prep failed: PBF file not found: $(PBF)"; \
		echo "tip: place dataset under data/pbf/ or pass PBF=..."; \
		exit 1; \
	fi; \
	echo "[2/3] Parsing PBF and building cache (this can take several seconds)..."; \
	if ! ./$(BINARY) --pbf=$(PBF) --save-cache=$(CACHE) >$$log_file 2>&1; then \
		echo "prep failed: parser/cache build error"; \
		tail -n 60 $$log_file; \
		exit 1; \
	fi; \
	end_ts=$$(date +%s); \
	elapsed=$$((end_ts - start_ts)); \
	parse_line=$$(grep -m1 "^Parse seconds:" $$log_file || true); \
	parse_value=$${parse_line#Parse seconds: }; \
	echo "[3/3] ready to load server"; \
	if [ -n "$$parse_value" ]; then \
		echo "parse seconds : $${parse_value}s"; \
	else \
		echo "parse seconds : $${elapsed}s"; \
	fi

server:
	./$(BINARY) --load-cache=$(CACHE) --serve --port=$(PORT)

ui:
	@echo "Open GUI: http://127.0.0.1:$(UI_PORT)/frontend/index.html"
	python3 -m http.server $(UI_PORT)

up:
	@echo "Run in two terminals:"
	@echo "  Terminal 1: make server"
	@echo "  Terminal 2: make ui"
	@echo "Then open: http://127.0.0.1:$(UI_PORT)/frontend/index.html"
