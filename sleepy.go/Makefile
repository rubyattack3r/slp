.PHONY: all build test clean example update-bindings

# Default target
all: build test

# Update bindings fetches the latest amalgamated C files from the root
update-bindings:
	@echo "Updating C bindings from project root..."
	cd .. && make amalgamate
	cp ../dist/sleepy.h ./
	cp ../dist/sleepy.c ./

# Build the wrapper
build:
	go build -v ./...

# Test the parser
test:
	go test -v ./...

# Run the summarize BOFs example against SA.cna
example:
	go run examples/summarize_bofs.go examples/SA.cna

# Run the replace_function example against SA.cna
example-replace:
	go run examples/replace_function.go openf artifact_open examples/SA.cna

# Clean artifacts
clean:
	go clean
	rm -f test_out.txt
