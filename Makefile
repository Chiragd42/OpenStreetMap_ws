.PHONY: prep server ui up

BUILD_DIR := build-release
BINARY := $(BUILD_DIR)/osm_geocoder
PBF ?= data/pbf/stuttgart-regbez-260416.osm.pbf
CACHE ?= data/cache/stuttgart.bin
PORT ?= 8080
UI_PORT ?= 5500

prep:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD_DIR) -j
	./$(BINARY) --pbf=$(PBF) --save-cache=$(CACHE)

server:
	./$(BINARY) --load-cache=$(CACHE) --serve --port=$(PORT)

ui:
	python3 -m http.server $(UI_PORT)

up:
	@echo "Run in two terminals:"
	@echo "  Terminal 1: make server"
	@echo "  Terminal 2: make ui"
	@echo "Then open: http://127.0.0.1:$(UI_PORT)/frontend/index.html"
