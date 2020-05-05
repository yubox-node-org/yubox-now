YUBOX_PROJECT=$(shell basename *.ino .ino)

all: $(YUBOX_PROJECT).tar.gz

$(YUBOX_PROJECT).tar.gz: data/manifest.txt $(YUBOX_PROJECT).ino.nodemcu-32s.bin
	rm -rf dist/
	mkdir dist
	cp data/* $(YUBOX_PROJECT).ino.nodemcu-32s.bin dist/
	rm -f $(YUBOX_PROJECT).tar.gz
	tar --transform 's,^dist/,,' -cf $(YUBOX_PROJECT).tar dist/*
	gzip -9 $(YUBOX_PROJECT).tar
	rm -rf dist/

spiffs: data/manifest.txt

data/manifest.txt: modules.txt
	rm -rf data/
	mkdir data/
	$(YF)/yubox-framework-assemble $(YF)/data-template:./data-template $(shell cat modules.txt)
	gzip -9 data/*.htm data/*.js data/*.css

clean:
	rm -f dist/*
	rm -f data/*

