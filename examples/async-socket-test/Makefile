# Place amlc.jar in this folder or change value.
AMLC:=amlc.jar
CMD=java -jar $(AMLC)

# Default to the linux-x64 libc build.
build:
	$(CMD) build . -bt linux-x64 -ll5 -maxOneError

build-amigaos:
	$(CMD) build . -bt amigaos-docker -ll5

build-fld: # force load dependencies
	$(CMD) build . -fld -bt linux-x64 -ll5 -maxOneError

# Run the amigaos binary headlessly inside Docker via Amiberry. Uses the
# shared amiberry:latest image baked by ../../../../am-lang-compiler-code/
# amiberry-docker/, but with a local .uae template that enables
# bsdsocket_emu so the Amiga side can OpenLibrary "bsdsocket.library"
# and do real DNS + TCP through the host stack.
#
# KICK31 defaults to the same path the user's FS-UAE setup uses; override
# on the command line if your ROM lives elsewhere:
#   make run-amigaos-amiberry KICK31=/path/to/kick31.rom
KICK31 ?= $(HOME)/Documents/FS-UAE/Kickstarts/kick31.rom
run-amigaos-amiberry:
	KICK31="$(KICK31)" ./amiberry/run-headless.sh

clean:
	$(CMD) clean . -bt linux-x64
	$(CMD) clean . -bt amigaos-docker
