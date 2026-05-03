.PHONY: prep server ui up

BUILD_DIR := build-release
BINARY := $(BUILD_DIR)/osm_geocoder
PBF ?= data/pbf/baden-wuerttemberg-260430.osm.pbf
CACHE ?= data/cache/baden-wuerttemberg-260430.bin
PORT ?= 8080
UI_PORT ?= 5500

prep:
	@start_ts=$$(date +%s); \
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release >/dev/null; \
	cmake --build $(BUILD_DIR) -j >/dev/null; \
	mkdir -p $$(dirname $(CACHE)); \
	./$(BINARY) --pbf=$(PBF) --save-cache=$(CACHE) >/tmp/osm_prep.log 2>&1; \
	end_ts=$$(date +%s); \
	elapsed=$$((end_ts - start_ts)); \
	parse_line=$$(grep -m1 "^Parse seconds:" /tmp/osm_prep.log || true); \
	parse_value=$${parse_line#Parse seconds: }; \
	echo "ready to load server"; \
	if [ -n "$$parse_value" ]; then \
		echo "parse seconds : $${parse_value}s"; \
	else \
		echo "parse seconds : $${elapsed}s"; \
	fi

server:
	@start_ts=$$(date +%s); \
	log_file=/tmp/osm_server.log; \
	./$(BINARY) --load-cache=$(CACHE) --serve --port=$(PORT) >$$log_file 2>&1 & \
	server_pid=$$!; \
	trap 'kill $$server_pid 2>/dev/null || true' INT TERM EXIT; \
	for i in $$(seq 1 120); do \
		if curl -fsS "http://127.0.0.1:$(PORT)/stats" >/dev/null 2>&1; then \
			break; \
		fi; \
		sleep 1; \
	done; \
	end_ts=$$(date +%s); \
	elapsed=$$((end_ts - start_ts)); \
	echo "server load seconds : $${elapsed}s"; \
	wait $$server_pid

ui:
	python3 -m http.server $(UI_PORT)

up:
	@echo "Run in two terminals:"
	@echo "  Terminal 1: make server"
	@echo "  Terminal 2: make ui"
	@echo "Then open: http://127.0.0.1:$(UI_PORT)/frontend/index.html"
