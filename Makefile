# ====================== CONFIGURATION ======================

# Compiler and standard flags
CC = gcc
CSTD = -std=c99
COPT = -g -O
CWARN = 

# Project directories
SRC_DIR = src
BUILD_DIR = build
WAVESHARE_DIR = lib/e-Paper/RaspberryPi_JetsonNano/c/lib

# Include directories
INCLUDE_DIRS = -Isrc \
               -I$(WAVESHARE_DIR)/Config \
               -I$(WAVESHARE_DIR)/e-Paper \
               -I$(WAVESHARE_DIR)/GUI \
               -I$(WAVESHARE_DIR)/Fonts

# Platform-specific definitions
PLATFORM_DEFS = -DPROJECT_ROOT=\"$(shell pwd)\" \
                -DUSE_LGPIO_LIB \
                -DRPI \
                -DDEBUG

# External libraries
PKG_CFLAGS = $(shell pkg-config --cflags cairo freetype2)
PKG_LIBS = $(shell pkg-config --libs cairo freetype2)
LIBS = -lcurl -lcjson -lpthread $(PKG_LIBS) -lm -llgpio

# ====================== COMPILE FLAGS ======================

# Main project compilation flags
CFLAGS = $(CSTD) $(COPT) $(CWARN) $(INCLUDE_DIRS) $(PLATFORM_DEFS) $(PKG_CFLAGS)

# Waveshare library flags (suppress warnings for third-party code)
WAVESHARE_CFLAGS = $(CSTD) $(COPT) -D_GNU_SOURCE $(INCLUDE_DIRS) $(PLATFORM_DEFS) -w

# ====================== SOURCE FILES ======================

# Main project sources
SOURCES = main.c \
          weather.c \
          menu.c \
          calendar.c \
          display_stdout.c \
          http.c \
          dashboard_render.c \
          display_dashboard.c \
          logging.c

# Waveshare library sources
WAVESHARE_SOURCES = $(WAVESHARE_DIR)/Config/DEV_Config.c \
                    $(WAVESHARE_DIR)/Config/dev_hardware_SPI.c \
                    $(WAVESHARE_DIR)/e-Paper/EPD_7in5_V2.c \
                    $(WAVESHARE_DIR)/GUI/GUI_Paint.c \
                    $(WAVESHARE_DIR)/GUI/GUI_BMPfile.c \
                    $(WAVESHARE_DIR)/Fonts/font20.c \
                    $(WAVESHARE_DIR)/Fonts/font24.c

# ====================== BUILD TARGETS ======================

TARGET = $(BUILD_DIR)/dashboard
OBJECTS = $(addprefix $(BUILD_DIR)/, $(SOURCES:.c=.o)) \
          $(addprefix $(BUILD_DIR)/, $(notdir $(WAVESHARE_SOURCES:.c=.o)))

# ====================== BUILD RULES ======================

# Default target
all: $(BUILD_DIR) $(TARGET)

# Create build directory
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# Link final executable
$(TARGET): $(OBJECTS)
	@echo "Linking $(TARGET)..."
	@$(CC) $(CFLAGS) -o $(TARGET) $(OBJECTS) $(LIBS)

# Compile main project source files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "Compiling $<..."
	@$(CC) $(CFLAGS) -c $< -o $@

# ====================== WAVESHARE LIBRARY RULES ======================

# Generic rule for Waveshare library compilation
define WAVESHARE_RULE
$(BUILD_DIR)/$(notdir $(basename $(1))).o: $(1)
	@echo "Compiling Waveshare: $$(notdir $$<)..."
	@$$(CC) $$(WAVESHARE_CFLAGS) -c $$< -o $$@
endef

# Generate rules for each Waveshare source file
$(foreach src,$(WAVESHARE_SOURCES),$(eval $(call WAVESHARE_RULE,$(src))))

# ====================== UTILITY TARGETS ======================

# Clean build artifacts
clean:
	@echo "Cleaning build directory..."
	@rm -rf $(BUILD_DIR)

# Install system dependencies
install-deps:
	@echo "Installing system dependencies..."
	@sudo apt-get update
	@sudo apt-get install -y libcurl4-openssl-dev libcjson-dev libcairo2-dev libfreetype6-dev liblgpio-dev

# Run in debug mode
test: $(TARGET)
	@echo "Running dashboard in debug mode..."
	@cd $(BUILD_DIR) && ./dashboard --debug

# Build and run display time test
time-test: $(BUILD_DIR)/display_time_test
	@echo "Running display time test..."
	@cd $(BUILD_DIR) && ./display_time_test

# Build display time test binary
$(BUILD_DIR)/display_time_test: $(SRC_DIR)/display_time_test.c $(addprefix $(BUILD_DIR)/, $(notdir $(WAVESHARE_SOURCES:.c=.o)))
	@echo "Building display_time_test..."
	@$(CC) $(WAVESHARE_CFLAGS) -o $@ $(SRC_DIR)/display_time_test.c $(addprefix $(BUILD_DIR)/, $(notdir $(WAVESHARE_SOURCES:.c=.o))) $(LIBS)

# Show build configuration
config:
	@echo "Build Configuration:"
	@echo "  CC: $(CC)"
	@echo "  CFLAGS: $(CFLAGS)"
	@echo "  LIBS: $(LIBS)"
	@echo "  TARGET: $(TARGET)"
	@echo "  SOURCES: $(SOURCES)"

# Help target
help:
	@echo "Available targets:"
	@echo "  all          - Build the dashboard (default)"
	@echo "  clean        - Remove build artifacts"
	@echo "  test         - Build and run in debug mode"
	@echo "  time-test    - Build and run display time test"
	@echo "  install-deps - Install system dependencies"
	@echo "  config       - Show build configuration"
	@echo "  help         - Show this help message"

.PHONY: all clean install-deps test time-test config help