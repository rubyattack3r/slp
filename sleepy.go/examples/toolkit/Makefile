# Sleepy OPSEC Toolkit Makefile

BINARY_NAME=toolkit
DIST_DIR=dist
PKG=./cmd/toolkit

.PHONY: all build clean run help

all: build

build:
	@mkdir -p $(DIST_DIR)
	go build -buildvcs=false -o $(DIST_DIR)/$(BINARY_NAME) $(PKG)

clean:
	rm -rf $(DIST_DIR)
	rm -f opsec.cna

run: build
	./$(DIST_DIR)/$(BINARY_NAME) manage third-party/

generate: build
	./$(DIST_DIR)/$(BINARY_NAME) generate

help:
	@echo "Available targets:"
	@echo "  build    - Build the unified toolkit binary to $(DIST_DIR)/"
	@echo "  clean    - Remove build artifacts and generated scripts"
	@echo "  run      - Build and launch the Interactive Manager on third-party/"
	@echo "  generate - Build and generate opsec.cna from opsec.json"
	@echo "  help     - Show this help message"
