.PHONY: prep prep-germany validate-kiel server ui up

BUILD_DIR := build-release
BINARY := $(BUILD_DIR)/osm_geocoder
PBF_DIR ?= data/pbf
CACHE_DIR ?= data/cache
DATASET ?= auto
DETECTED_PBF := $(firstword $(wildcard $(PBF_DIR)/*.osm.pbf))
PBF ?= $(DETECTED_PBF)
DATASET_NAME := $(if $(filter auto,$(DATASET)),$(notdir $(basename $(basename $(PBF)))),$(DATASET))
CACHE ?= $(CACHE_DIR)/$(DATASET_NAME).bin
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
	if [ -z "$(PBF)" ] || [ ! -f "$(PBF)" ]; then \
		echo "prep failed: no .osm.pbf file found under $(PBF_DIR)/"; \
		echo "tip: place dataset under $(PBF_DIR)/ or pass PBF=/path/to/file.osm.pbf"; \
		exit 1; \
	fi; \
	echo "Using PBF   : $(PBF)"; \
	echo "Using CACHE : $(CACHE)"; \
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
	@echo "Using CACHE : $(CACHE)"
	./$(BINARY) --load-cache=$(CACHE) --serve --port=$(PORT)

prep-germany:
	@if [ "$(origin PBF)" != "command line" ] || [ -z "$(PBF)" ] || [ ! -f "$(PBF)" ]; then \
		echo "Germany PBF not found. Download it explicitly, then run:"; \
		echo "  make prep-germany PBF=data/pbf/germany-latest.osm.pbf"; \
		exit 1; \
	fi
	$(MAKE) prep DATASET=germany PBF="$(PBF)" CACHE="$(CACHE_DIR)/germany.bin"

validate-kiel:
	@if [ ! -f "$(CACHE)" ]; then echo "Cache not found: $(CACHE)"; exit 1; fi
	./$(BINARY) --load-cache="$(CACHE)" --no-merge-streets --test-geocode-query="Kaistrasse 5 Kiel"
	./$(BINARY) --load-cache="$(CACHE)" --no-merge-streets --test-geocode-query="Closest Park to Kaistrasse 5 Kiel"

ui:
	@echo "Open GUI: http://127.0.0.1:$(UI_PORT)/frontend/index.html"
	python3 -m http.server $(UI_PORT)

up:
	@echo "Run in two terminals:"
	@echo "  Terminal 1: make server"
	@echo "  Terminal 2: make ui"
	@echo "Then open: http://127.0.0.1:$(UI_PORT)/frontend/index.html"
