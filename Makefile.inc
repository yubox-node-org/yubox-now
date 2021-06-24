YUBOX_PROJECT=$(shell basename "`pwd`")
SERIALPORT=/dev/ttyUSB0
ARDUINO_INSTALL=system
UNAME_S=$(shell uname -s)

ARDUINO_HOME=$(HOME)
ifeq ($(UNAME_S),Linux)
	ifeq ($(ARDUINO_INSTALL),system)
		ARDUINO_BUILDER=/usr/bin/arduino-builder
		ARDUINO_STDHARDWARE=/usr/share/arduino/hardware
		ARDUINO_TOOLSPARAM=
		ARDUINO_BUILTIN_LIBS=
	else
		ARDUINO_BUILDER=$(ARDUINO_INSTALL)/arduino-builder
		ARDUINO_STDHARDWARE=$(ARDUINO_INSTALL)/hardware
		ARDUINO_TOOLSPARAM=-tools $(ARDUINO_INSTALL)/tools-builder -tools $(ARDUINO_INSTALL)/hardware/tools/avr
		ARDUINO_BUILTIN_LIBS=-built-in-libraries $(ARDUINO_INSTALL)/libraries
	endif
	ARDUINO_LIBUSR=$(ARDUINO_HOME)/Arduino/libraries
	ARDUINO_PKGS=$(ARDUINO_HOME)/.arduino15/packages
	ARDUINO_CTAGS=
endif
ifeq ($(UNAME_S),Darwin)
	ARDUINO_BUILDER=$(HOME)/arduino-builder/arduino-builder
	ARDUINO_STDHARDWARE=
	ARDUINO_TOOLSPARAM=
	ARDUINO_BUILTIN_LIBS=
	ARDUINO_LIBUSR=$(ARDUINO_HOME)/Documents/Arduino/libraries
	ARDUINO_PKGS=$(ARDUINO_HOME)/Library/Arduino15/packages
	ARDUINO_CTAGS=-prefs=runtime.tools.ctags.path=/Applications/Arduino.app/Contents/Java/tools-builder/ctags/5.8-arduino11/
endif
ARDUINO_ESP32=$(ARDUINO_PKGS)/esp32
ESP32_ARDUINOVER=$(shell basename $(ARDUINO_ESP32)/hardware/esp32/*)
ESP32_PARTDIR=$(ARDUINO_ESP32)/hardware/esp32/$(ESP32_ARDUINOVER)/tools/partitions

ESP32_BOARD=esp32
ESP32_BOARDS=$(ARDUINO_ESP32)/hardware/esp32/$(ESP32_ARDUINOVER)/boards.txt
ESP32_BOARD_PINS=$(ARDUINO_ESP32)/hardware/esp32/$(ESP32_ARDUINOVER)/variants/$(ESP32_BOARD)/pins_arduino.h

# ESP32_PARTCONF can be one of the following:
# 4MB boards: default no_ota min_spiffs huge_app noota_3g (defaultffat noota_ffat noota_3gffat)
# 8MB boards: default_8MB
# 16MB boards: fatflash
ESP32_PARTCONF?=$(shell grep ^$(ESP32_BOARD).build.partitions $(ESP32_BOARDS) | cut -f 2 -d =)
ifneq ($(wildcard partitions.csv),)
	ESP32_PARTCONF_CSVTABLE?=partitions.csv
else
	ESP32_PARTCONF_CSVTABLE?=$(ESP32_PARTDIR)/$(ESP32_PARTCONF).csv
endif
ESP32_OFFSET_OTADATA=$(shell grep otadata $(ESP32_PARTCONF_CSVTABLE) | cut -f 4 -d , )
ESP32_OFFSET_APP0=$(shell grep ota_0 $(ESP32_PARTCONF_CSVTABLE) | cut -f 4 -d , )
ESP32_OFFSET_SPIFFS=$(shell grep spiffs $(ESP32_PARTCONF_CSVTABLE) | cut -f 4 -d , )
ESP32_SIZE_SPIFFS_HEX=$(shell grep spiffs $(ESP32_PARTCONF_CSVTABLE) | cut -f 5 -d , )
ESP32_SIZE_SPIFFS=$(shell printf "%d\n" $(ESP32_SIZE_SPIFFS_HEX))

XTENSA_GCCVER=$(shell basename $(ARDUINO_ESP32)/tools/xtensa-esp32-elf-gcc/*)
ESPTOOL_PYVER=$(shell basename $(ARDUINO_ESP32)/tools/esptool_py/*)
MKSPIFFS_VER=$(shell basename $(ARDUINO_ESP32)/tools/mkspiffs/*)

# ESP32_DEBUGLEVEL should be one of: none error warn info debug verbose
ESP32_DEBUGLEVEL=none
ESP32_PSRAM=disabled
ifeq ($(ESP32_BOARD),esp32wrover)
	ESP32_FQBN=esp32:esp32:$(ESP32_BOARD):PartitionScheme=$(ESP32_PARTCONF),FlashMode=dio,FlashFreq=80,UploadSpeed=921600,LoopCore=1,EventsCore=1,DebugLevel=$(ESP32_DEBUGLEVEL)
else
	ESP32_FQBN=esp32:esp32:$(ESP32_BOARD):PSRAM=$(ESP32_PSRAM),PartitionScheme=$(ESP32_PARTCONF),CPUFreq=240,FlashMode=dio,FlashFreq=80,FlashSize=4M,UploadSpeed=921600,LoopCore=1,EventsCore=1,DebugLevel=$(ESP32_DEBUGLEVEL)
endif

ifeq ($(ARDUINO_STDHARDWARE),)
	ARDUINO_STDHARDWARE_PARAM=
else
	ARDUINO_STDHARDWARE_PARAM=-hardware $(ARDUINO_STDHARDWARE)
endif

ESP32_BOOTLOADER_PATH=$(ARDUINO_ESP32)/hardware/esp32/$(ESP32_ARDUINOVER)/tools/sdk/esp32/bin/bootloader_dio_80m.bin

ARDUINO_BUILD_EXTRA_FLAGS_PARAM=
ifneq ($(ARDUINO_BUILD_EXTRA_FLAGS),)
ARDUINO_BUILD_EXTRA_FLAGS_PARAM=-prefs=build.defines="$(ARDUINO_BUILD_EXTRA_FLAGS)"
endif

YUBOXFILES=$(YUBOX_PROJECT).ino $(if $(wildcard *.cpp),*.cpp,) $(if $(wildcard *.h),*.h,)

all: $(YUBOX_PROJECT).tar.gz

testpartconf: $(ESP32_PARTCONF_CSVTABLE) $(ESP32_BOARD_PINS)
	echo "ESP32_FQBN " $(ESP32_FQBN)
	echo "ESP32_OFFSET_OTADATA " $(ESP32_OFFSET_OTADATA)
	echo "ESP32_OFFSET_APP0 " $(ESP32_OFFSET_APP0)
	echo "ESP32_OFFSET_SPIFFS " $(ESP32_OFFSET_SPIFFS)
	echo "ESP32_SIZE_SPIFFS_HEX " $(ESP32_SIZE_SPIFFS_HEX)
	echo "ESP32_SIZE_SPIFFS " $(ESP32_SIZE_SPIFFS)

$(YUBOX_PROJECT).tar.gz: data/manifest.txt $(YUBOX_PROJECT).ino.$(ESP32_BOARD).bin
	rm -rf dist/
	mkdir dist
	cp data/* $(YUBOX_PROJECT).ino.$(ESP32_BOARD).bin dist/
	rm -f $(YUBOX_PROJECT).tar.gz
	cd dist && tar -cf ../$(YUBOX_PROJECT).tar * && cd ..
	gzip -9 $(YUBOX_PROJECT).tar
	rm -rf dist/

data/manifest.txt: modules.txt $(YF)/data-template $(YF)/data-template/* $(YF)/data-template/*/* ./data-template ./data-template/* ./data-template/*/*
	rm -rf data/
	mkdir data/
	$(YF)/yubox-framework-assemble ./data-template $(shell cat modules.txt)
	gzip -9 data/*.htm data/*.js data/*.css

$(YUBOX_PROJECT).ino.$(ESP32_BOARD).bin: build/$(YUBOX_PROJECT).ino.bin
	cp build/$(YUBOX_PROJECT).ino.bin $(YUBOX_PROJECT).ino.$(ESP32_BOARD).bin

build/$(YUBOX_PROJECT).ino.bin: $(YUBOXFILES) $(ESP32_BOARD_PINS)
	mkdir -p build/_cache
	$(ARDUINO_BUILDER) \
		-compile \
		-logger=machine \
		$(ARDUINO_STDHARDWARE_PARAM) \
		-hardware $(ARDUINO_PKGS) \
		$(ARDUINO_TOOLSPARAM) \
		-tools $(ARDUINO_PKGS) \
		$(ARDUINO_BUILTIN_LIBS) \
		-libraries $(ARDUINO_LIBUSR) \
		-fqbn=$(ESP32_FQBN) \
		-ide-version=10813 \
		-build-path "`pwd`/build" \
		-warnings=default \
		-build-cache "`pwd`/build/_cache" \
		-prefs=build.warn_data_percentage=75 \
		-prefs=runtime.tools.esptool_py.path=$(ARDUINO_ESP32)/tools/esptool_py/$(ESPTOOL_PYVER) \
		-prefs=runtime.tools.esptool_py-$(ESPTOOL_PYVER).path=$(ARDUINO_ESP32)/tools/esptool_py/$(ESPTOOL_PYVER) \
		-prefs=runtime.tools.mkspiffs.path=$(ARDUINO_ESP32)/tools/mkspiffs/$(MKSPIFFS_VER) \
		-prefs=runtime.tools.mkspiffs-$(MKSPIFFS_VER).path=$(ARDUINO_ESP32)/tools/mkspiffs/$(MKSPIFFS_VER) \
		-prefs=runtime.tools.xtensa-esp32-elf-gcc.path=$(ARDUINO_ESP32)/tools/xtensa-esp32-elf-gcc/$(XTENSA_GCCVER) \
		-prefs=runtime.tools.xtensa-esp32-elf-gcc-$(XTENSA_GCCVER).path=$(ARDUINO_ESP32)/tools/xtensa-esp32-elf-gcc/$(XTENSA_GCCVER) \
		$(ARDUINO_CTAGS) \
		$(ARDUINO_BUILD_EXTRA_FLAGS_PARAM) \
		./$(YUBOX_PROJECT).ino

clean:
	rm -f dist/*
	rm -f data/*
	rm -rf build/
	rm -f $(YUBOX_PROJECT).ino.*.bin $(YUBOX_PROJECT).tar.gz

codeupload: build/$(YUBOX_PROJECT).ino.bin build/$(YUBOX_PROJECT).ino.partitions.bin $(ESP32_PARTCONF_CSVTABLE)
	python $(ARDUINO_ESP32)/tools/esptool_py/$(ESPTOOL_PYVER)/esptool.py \
		--chip esp32 \
		--port $(SERIALPORT) \
		--baud 921600 \
		--before default_reset \
		--after hard_reset \
		write_flash -z --flash_mode dio --flash_freq 80m --flash_size detect \
		0x1000 $(ESP32_BOOTLOADER_PATH) \
		0x8000 build/$(YUBOX_PROJECT).ino.partitions.bin \
		$(ESP32_OFFSET_OTADATA) $(ESP32_PARTDIR)/boot_app0.bin \
		$(ESP32_OFFSET_APP0) build/$(YUBOX_PROJECT).ino.bin

# -s SPISIZE corresponde a la configuración default de ~/.arduino15/packages/esp32/hardware/esp32/$(ESP32_ARDUINOVER)/tools/partitions/default.csv
build/$(YUBOX_PROJECT).spiffs: data/manifest.txt $(ESP32_PARTCONF_CSVTABLE)
	$(ARDUINO_ESP32)/tools/mkspiffs/$(MKSPIFFS_VER)/mkspiffs \
		-c data \
		-p 256 -b 4096 \
		-s $(ESP32_SIZE_SPIFFS) \
		build/$(YUBOX_PROJECT).spiffs

dataupload: build/$(YUBOX_PROJECT).spiffs
	python $(ARDUINO_ESP32)/tools/esptool_py/$(ESPTOOL_PYVER)/esptool.py \
		--chip esp32 \
		--port $(SERIALPORT) \
		--baud 921600 \
		--before default_reset \
		--after hard_reset \
		write_flash -z --flash_mode dio --flash_freq 80m --flash_size detect \
		$(ESP32_OFFSET_SPIFFS) build/$(YUBOX_PROJECT).spiffs

fullupload: build/$(YUBOX_PROJECT).ino.bin build/$(YUBOX_PROJECT).ino.partitions.bin build/$(YUBOX_PROJECT).spiffs
	python $(ARDUINO_ESP32)/tools/esptool_py/$(ESPTOOL_PYVER)/esptool.py \
		--chip esp32 \
		--port $(SERIALPORT) \
		--baud 921600 \
		--before default_reset \
		--after hard_reset \
		write_flash -z --flash_mode dio --flash_freq 80m --flash_size detect \
		0x1000 $(ESP32_BOOTLOADER_PATH) \
		0x8000 build/$(YUBOX_PROJECT).ino.partitions.bin \
		$(ESP32_OFFSET_OTADATA) $(ESP32_PARTDIR)/boot_app0.bin \
		$(ESP32_OFFSET_APP0) build/$(YUBOX_PROJECT).ino.bin \
		$(ESP32_OFFSET_SPIFFS) build/$(YUBOX_PROJECT).spiffs

