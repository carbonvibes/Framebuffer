CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2
LDFLAGS = 

# Target executable
TARGET = framebuffer_extractor

# Source files
SOURCES = framebuffer_extractor.c

# Object files
OBJECTS = $(SOURCES:.c=.o)

# Default target
all: $(TARGET)

# Build the executable
$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)
	@echo "Built $(TARGET) successfully"

# Compile source files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Install target (copy to /usr/local/bin)
install: $(TARGET)
	sudo cp $(TARGET) /usr/local/bin/
	sudo chmod +x /usr/local/bin/$(TARGET)
	@echo "Installed $(TARGET) to /usr/local/bin/"

# Clean build artifacts
clean:
	rm -f $(OBJECTS) $(TARGET)
	rm -f *.raw *.ppm framebuffer_*.raw framebuffer_*.ppm
	@echo "Cleaned build artifacts and output files"

# Run basic framebuffer extraction
test: $(TARGET)
	@echo "Testing framebuffer extraction (requires root privileges)..."
	sudo ./$(TARGET) --analyze --save

# Run monitoring mode
monitor: $(TARGET)
	@echo "Starting framebuffer monitoring (requires root privileges)..."
	sudo ./$(TARGET) --monitor

# Make eBPF scripts executable
ebpf-setup:
	chmod +x *.bt
	@echo "Made eBPF scripts executable"

# Run basic eBPF framebuffer tracing
trace-basic: ebpf-setup
	@echo "Starting basic framebuffer tracing..."
	sudo bpftrace framebuffer_tracer.bt

# Run advanced eBPF data extraction
trace-advanced: ebpf-setup
	@echo "Starting advanced framebuffer data extraction..."
	sudo bpftrace framebuffer_data_extractor.bt

# Run pixel capture tracing
trace-pixels: ebpf-setup
	@echo "Starting pixel data capture..."
	sudo bpftrace pixel_data_capture.bt

# Combined test - run C extractor and eBPF tracer in parallel
combined-test: $(TARGET) ebpf-setup
	@echo "Running combined framebuffer extraction test..."
	@echo "This will run the C extractor and eBPF tracer simultaneously"
	sudo ./$(TARGET) --monitor &
	sudo bpftrace framebuffer_tracer.bt

# View captured images (if ImageMagick is installed)
view-images:
	@if command -v display >/dev/null 2>&1; then \
		for img in *.ppm; do \
			if [ -f "$$img" ]; then \
				echo "Displaying $$img..."; \
				display "$$img" & \
			fi; \
		done; \
	else \
		echo "ImageMagick not installed. You can view .ppm files with any image viewer."; \
		ls -la *.ppm 2>/dev/null || echo "No .ppm files found"; \
	fi

# Show help
help:
	@echo "Framebuffer Extraction Toolkit"
	@echo "=============================="
	@echo ""
	@echo "Available targets:"
	@echo "  all           - Build the framebuffer extractor"
	@echo "  clean         - Clean build artifacts and output files"
	@echo "  install       - Install the extractor to /usr/local/bin"
	@echo "  test          - Run basic framebuffer extraction test"
	@echo "  monitor       - Run framebuffer monitoring"
	@echo "  ebpf-setup    - Make eBPF scripts executable"
	@echo "  trace-basic   - Run basic eBPF framebuffer tracing"
	@echo "  trace-advanced- Run advanced eBPF data extraction"
	@echo "  trace-pixels  - Run pixel data capture tracing"
	@echo "  combined-test - Run C extractor and eBPF tracer together"
	@echo "  view-images   - View captured images (requires ImageMagick)"
	@echo "  help          - Show this help message"
	@echo ""
	@echo "Requirements:"
	@echo "  - Root privileges for framebuffer and eBPF access"
	@echo "  - bpftrace installed for eBPF scripts"
	@echo "  - /dev/fb0 available (framebuffer device)"
	@echo ""
	@echo "Usage examples:"
	@echo "  make test                    # Quick test"
	@echo "  make trace-basic            # eBPF tracing"
	@echo "  make combined-test          # Full analysis"

.PHONY: all clean install test monitor ebpf-setup trace-basic trace-advanced trace-pixels combined-test view-images help
