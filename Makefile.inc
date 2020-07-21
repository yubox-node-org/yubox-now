YUBOX_PROJECT=$(shell basename *.ino .ino)
ARDUINO_HOME=$(HOME)
SERIALPORT=/dev/ttyUSB0

YUBOXFILES=$(YUBOX_PROJECT).ino $(if $(wildcard *.cpp),*.cpp,) $(if $(wildcard *.h),*.h,)

all: $(YUBOX_PROJECT).tar.gz

$(YUBOX_PROJECT).tar.gz: data/manifest.txt $(YUBOX_PROJECT).ino.nodemcu-32s.bin
	rm -rf dist/
	mkdir dist
	cp data/* $(YUBOX_PROJECT).ino.nodemcu-32s.bin dist/
	rm -f $(YUBOX_PROJECT).tar.gz
	tar --transform 's,^dist/,,' -cf $(YUBOX_PROJECT).tar dist/*
	gzip -9 $(YUBOX_PROJECT).tar
	rm -rf dist/

data/manifest.txt: modules.txt $(YF)/data-template $(YF)/data-template/* $(YF)/data-template/*/* ./data-template ./data-template/* ./data-template/*/*
	rm -rf data/
	mkdir data/
	$(YF)/yubox-framework-assemble $(YF)/data-template:./data-template $(shell cat modules.txt)
	gzip -9 data/*.htm data/*.js data/*.css

$(YUBOX_PROJECT).ino.nodemcu-32s.bin: build/$(YUBOX_PROJECT).ino.bin
	cp build/$(YUBOX_PROJECT).ino.bin $(YUBOX_PROJECT).ino.nodemcu-32s.bin

build/$(YUBOX_PROJECT).ino.bin: $(YUBOXFILES)
	mkdir -p build/_cache
	/usr/bin/arduino-builder \
		-compile \
		-logger=machine \
		-hardware /usr/share/arduino/hardware \
		-hardware $(ARDUINO_HOME)/.arduino15/packages \
		-tools $(ARDUINO_HOME)/.arduino15/packages \
		-libraries $(ARDUINO_HOME)/Arduino/libraries \
		-fqbn=esp32:esp32:nodemcu-32s:FlashFreq=80,UploadSpeed=921600 \
		-ide-version=10812 \
		-build-path `pwd`/build \
		-warnings=default \
		-build-cache `pwd`/build/_cache \
		-prefs=build.warn_data_percentage=75 \
		-prefs=runtime.tools.esptool_py.path=$(ARDUINO_HOME)/.arduino15/packages/esp32/tools/esptool_py/2.6.1 \
		-prefs=runtime.tools.esptool_py-2.6.1.path=$(ARDUINO_HOME)/.arduino15/packages/esp32/tools/esptool_py/2.6.1 \
		-prefs=runtime.tools.mkspiffs.path=$(ARDUINO_HOME)/.arduino15/packages/esp32/tools/mkspiffs/0.2.3 \
		-prefs=runtime.tools.mkspiffs-0.2.3.path=$(ARDUINO_HOME)/.arduino15/packages/esp32/tools/mkspiffs/0.2.3 \
		-prefs=runtime.tools.xtensa-esp32-elf-gcc.path=$(ARDUINO_HOME)/.arduino15/packages/esp32/tools/xtensa-esp32-elf-gcc/1.22.0-80-g6c4433a-5.2.0 \
		-prefs=runtime.tools.xtensa-esp32-elf-gcc-1.22.0-80-g6c4433a-5.2.0.path=$(ARDUINO_HOME)/.arduino15/packages/esp32/tools/xtensa-esp32-elf-gcc/1.22.0-80-g6c4433a-5.2.0 \
		./$(YUBOX_PROJECT).ino

clean:
	rm -f dist/*
	rm -f data/*
	rm -rf build/
	rm -f $(YUBOX_PROJECT).ino.nodemcu-32s.bin $(YUBOX_PROJECT).tar.gz

codeupload: build/$(YUBOX_PROJECT).ino.bin build/$(YUBOX_PROJECT).ino.partitions.bin
	python $(ARDUINO_HOME)/.arduino15/packages/esp32/tools/esptool_py/2.6.1/esptool.py \
		--chip esp32 \
		--port $(SERIALPORT) \
		--baud 921600 \
		--before default_reset \
		--after hard_reset \
		write_flash -z --flash_mode dio --flash_freq 80m --flash_size detect \
		0xe000 $(ARDUINO_HOME)/.arduino15/packages/esp32/hardware/esp32/1.0.4/tools/partitions/boot_app0.bin \
		0x1000 $(ARDUINO_HOME)/.arduino15/packages/esp32/hardware/esp32/1.0.4/tools/sdk/bin/bootloader_dio_80m.bin \
		0x10000 build/$(YUBOX_PROJECT).ino.bin \
		0x8000 build/$(YUBOX_PROJECT).ino.partitions.bin

# -s SPISIZE corresponde a la configuración default de ~/.arduino15/packages/esp32/hardware/esp32/1.0.4/tools/partitions/default.csv
build/$(YUBOX_PROJECT).spiffs: data/manifest.txt
	$(ARDUINO_HOME)/.arduino15/packages/esp32/tools/mkspiffs/0.2.3/mkspiffs \
		-c data \
		-p 256 -b 4096 \
		-s 1507328 \
		build/$(YUBOX_PROJECT).spiffs

# Las direcciones hexadecimales corresponden a la configuración default de ~/.arduino15/packages/esp32/hardware/esp32/1.0.4/tools/partitions/default.csv
dataupload: build/$(YUBOX_PROJECT).spiffs
	python $(ARDUINO_HOME)/.arduino15/packages/esp32/tools/esptool_py/2.6.1/esptool.py \
		--chip esp32 \
		--port $(SERIALPORT) \
		--baud 921600 \
		--before default_reset \
		--after hard_reset \
		write_flash -z --flash_mode dio --flash_freq 80m --flash_size detect \
		0x290000 build/$(YUBOX_PROJECT).spiffs

fullupload: build/$(YUBOX_PROJECT).ino.bin build/$(YUBOX_PROJECT).ino.partitions.bin build/$(YUBOX_PROJECT).spiffs
	python $(ARDUINO_HOME)/.arduino15/packages/esp32/tools/esptool_py/2.6.1/esptool.py \
		--chip esp32 \
		--port $(SERIALPORT) \
		--baud 921600 \
		--before default_reset \
		--after hard_reset \
		write_flash -z --flash_mode dio --flash_freq 80m --flash_size detect \
		0xe000 $(ARDUINO_HOME)/.arduino15/packages/esp32/hardware/esp32/1.0.4/tools/partitions/boot_app0.bin \
		0x1000 $(ARDUINO_HOME)/.arduino15/packages/esp32/hardware/esp32/1.0.4/tools/sdk/bin/bootloader_dio_80m.bin \
		0x10000 build/$(YUBOX_PROJECT).ino.bin \
		0x290000 build/$(YUBOX_PROJECT).spiffs \
		0x8000 build/$(YUBOX_PROJECT).ino.partitions.bin
