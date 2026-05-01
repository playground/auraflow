# Host-side tests for the AuraFlow C firmware modules.
# Each test_<module>.c is built into a standalone executable with all
# firmware modules under ../main/ linked in (except main.c, which has
# app_main()). Adding a new test is one line: append to TESTS.
#
# Usage:
#   make test     # build all + run each
#   make clean

CC      = clang
CFLAGS  = -std=c99 -Wall -Wextra -Werror -Wno-unused-function -O2 -I. -I../main

# All firmware modules under ../main/ except:
#   - main.c       — defines app_main(), conflicts with each test's main()
#   - *_esp.c      — ESP-IDF I/O code that doesn't compile on host
MODULES := $(filter-out ../main/main.c,$(wildcard ../main/*.c))
MODULES := $(filter-out %_esp.c,$(MODULES))
HEADERS := $(wildcard ../main/*.h)

TESTS = test_modbus test_tuf2000m test_ring_buffer test_nvs_config

# Static-pattern rule: every test_X depends on test_X.c, all firmware
# modules, and all headers. Recipe links the modules into the binary.
$(TESTS): test_%: test_%.c $(MODULES) $(HEADERS) test_helpers.h
	$(CC) $(CFLAGS) test_$*.c $(MODULES) -o $@

.PHONY: all test clean
all: $(TESTS)

test: $(TESTS)
	@total_fail=0; \
	for t in $(TESTS); do \
		echo "=== $$t ==="; \
		./$$t || total_fail=$$((total_fail + 1)); \
	done; \
	if [ $$total_fail -gt 0 ]; then \
		echo "\nFAIL: $$total_fail test binaries failed"; \
		exit 1; \
	fi

clean:
	rm -f $(TESTS)
