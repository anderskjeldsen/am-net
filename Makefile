# place amlc.jar in this folder or change value.
AMLC:=amlc.jar
CMD=java -jar $(AMLC)
LOGLEVEL:=1
MAXONEERROR:=false
RUNTIMELOGGING:=false
# Resolve transitive deps (am-lang-core, am-async, am-threading, …) from
# the workspace root instead of am-net's vendored `dependencies/` copies,
# which go stale relative to the workspace amlc.jar (e.g. the emitted
# `__retain_slot_read` runtime helper predates the bundled am-lang-core).
LPP:=../

build:
	$(CMD) build . -bt linux-x64 -lpp $(LPP) -ll5 -maxOneError

build-amigaos:
	$(CMD) build . -bt amigaos-docker -lpp $(LPP) -ll5

build-macos-arm:
	$(CMD) build . -bt macos-arm -lpp $(LPP) -ll5 -maxOneError

build-force-deps:
	$(CMD) build . -fld -bt linux-x64 -lpp $(LPP) ll 4

test:
# if MAXONEERROR is true, add -maxOneError flag
ifeq ($(MAXONEERROR),true)
	$(CMD) test . -bt linux-x64 -ll $(LOGLEVEL) -maxOneError
else
	$(CMD) test . -bt linux-x64 -ll $(LOGLEVEL)
endif

test-rl:
# if MAXONEERROR is true, add -maxOneError flag
ifeq ($(MAXONEERROR),true)
	$(CMD) test . -bt linux-x64 -ll $(LOGLEVEL) -maxOneError -rl -rlarc
else
	$(CMD) test . -bt linux-x64 -ll $(LOGLEVEL) -rl -rlarc
endif

lint:
	$(CMD) lint .


# Debug targets
gdb-test:
	gdb -batch -ex "set environment MALLOC_CHECK_=2" -ex "run" -ex "bt" -ex "quit" builds/test-bin/linux-x64/test_app

gdb-test-interactive:
	gdb builds/test-bin/linux-x64/test_app

gdb-app:
	gdb -batch -ex "run" -ex "bt" -ex "quit" builds/bin/linux-x64/app

gdb-app-interactive:
	gdb builds/bin/linux-x64/app

# Run test with high verbosity for debugging
test-verbose:
	$(CMD) test . -bt linux-x64 -ll 5

# Run test executable directly (bypassing compiler wrapper)
test-direct:
	./builds/test-bin/linux-x64/test_app
