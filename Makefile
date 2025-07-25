CC = gcc
CFLAGS = -g -O -Wall -Wextra -std=c99 -Isrc -Ilib/e-Paper/RaspberryPi_JetsonNano/c/lib/Config -Ilib/e-Paper/RaspberryPi_JetsonNano/c/lib/e-Paper -Ilib/e-Paper/RaspberryPi_JetsonNano/c/lib/GUI -Ilib/e-Paper/RaspberryPi_JetsonNano/c/lib/Fonts -DPROJECT_ROOT=\"$(shell pwd)\" -DUSE_LGPIO_LIB -DRPI -DDEBUG $(shell pkg-config --cflags cairo freetype2)
# Suppress warnings for third-party Waveshare library  
WAVESHARE_CFLAGS = -g -O -std=c99 -D_GNU_SOURCE -Isrc -Ilib/e-Paper/RaspberryPi_JetsonNano/c/lib/Config -Ilib/e-Paper/RaspberryPi_JetsonNano/c/lib/e-Paper -Ilib/e-Paper/RaspberryPi_JetsonNano/c/lib/GUI -Ilib/e-Paper/RaspberryPi_JetsonNano/c/lib/Fonts -DUSE_LGPIO_LIB -DRPI -DDEBUG -w
LIBS = -lcurl -lcjson -lpthread $(shell pkg-config --libs cairo freetype2) -lm -llgpio

# Directories
SRC_DIR = src
BUILD_DIR = build
CONFIG_DIR = config
WAVESHARE_DIR = lib/e-Paper/RaspberryPi_JetsonNano/c/lib

# Target and source files
TARGET = $(BUILD_DIR)/dashboard
SOURCES = main.c weather.c menu.c calendar.c display_stdout.c http.c dashboard_render.c display_dashboard.c logging.c
SRC_FILES = $(addprefix $(SRC_DIR)/, $(SOURCES))

# Waveshare library sources (RPI lgpio configuration)
WAVESHARE_SOURCES = $(WAVESHARE_DIR)/Config/DEV_Config.c \
                    $(WAVESHARE_DIR)/Config/dev_hardware_SPI.c \
                    $(WAVESHARE_DIR)/e-Paper/EPD_7in5_V2.c \
                    $(WAVESHARE_DIR)/GUI/GUI_Paint.c \
                    $(WAVESHARE_DIR)/GUI/GUI_BMPfile.c \
                    $(WAVESHARE_DIR)/Fonts/font20.c

OBJECTS = $(addprefix $(BUILD_DIR)/, $(SOURCES:.c=.o)) \
          $(addprefix $(BUILD_DIR)/, $(notdir $(WAVESHARE_SOURCES:.c=.o)))

# Default target
all: $(BUILD_DIR) $(TARGET)

# Create build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Dashboard target
$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJECTS) $(LIBS)

# Object files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Waveshare library object files (GPIO only, warnings suppressed)
$(BUILD_DIR)/DEV_Config.o: $(WAVESHARE_DIR)/Config/DEV_Config.c
	$(CC) $(WAVESHARE_CFLAGS) -c $< -o $@

$(BUILD_DIR)/dev_hardware_SPI.o: $(WAVESHARE_DIR)/Config/dev_hardware_SPI.c
	$(CC) $(WAVESHARE_CFLAGS) -c $< -o $@

$(BUILD_DIR)/EPD_7in5_V2.o: $(WAVESHARE_DIR)/e-Paper/EPD_7in5_V2.c
	$(CC) $(WAVESHARE_CFLAGS) -c $< -o $@

$(BUILD_DIR)/GUI_Paint.o: $(WAVESHARE_DIR)/GUI/GUI_Paint.c
	$(CC) $(WAVESHARE_CFLAGS) -c $< -o $@

$(BUILD_DIR)/GUI_BMPfile.o: $(WAVESHARE_DIR)/GUI/GUI_BMPfile.c
	$(CC) $(WAVESHARE_CFLAGS) -c $< -o $@

$(BUILD_DIR)/font20.o: $(WAVESHARE_DIR)/Fonts/font20.c
	$(CC) $(WAVESHARE_CFLAGS) -c $< -o $@

# Clean
clean:
	rm -rf $(BUILD_DIR)

# Install dependencies
install-deps:
	sudo apt-get update
	sudo apt-get install libcurl4-openssl-dev libcjson-dev libcairo2-dev libfreetype6-dev

# Test target
test: $(TARGET)
	cd $(BUILD_DIR) && ./dashboard --debug

.PHONY: all clean install-deps test