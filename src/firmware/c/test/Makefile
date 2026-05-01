# Host-side tests for the AuraFlow C firmware modules.
# Each test_<module>.c builds into test_<module> by linking the matching
# ../main/<module>.c. Add a new test by appending its name to TESTS.
#
# Usage:
#   make test     # build all + run each
#   make clean

CC      = clang
CFLAGS  = -std=c99 -Wall -Wextra -Werror -Wno-unused-function -O2 -I. -I../main

TESTS   = test_modbus

# Static-pattern rule: test_X depends on test_X.c + ../main/X.c
$(TESTS): test_%: test_%.c ../main/%.c test_helpers.h ../main/%.h
	$(CC) $(CFLAGS) test_$*.c ../main/$*.c -o $@

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
