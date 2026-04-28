CC      		= gcc
CFLAGS  		= -std=c11 -Wall -Wextra -Wpedantic
SANITIZE_FLAGS = -fsanitize=address,undefined -fno-omit-frame-pointer -g
VALGRIND       = valgrind
VALGRIND_FLAGS = --leak-check=full --error-exitcode=1 --track-origins=yes

# Flags passed to the linker. You will likely need to define this if you use
# additional libraries.
LDFLAGS =

LIB     = bun_parse.c
MAIN    = main.c
TEST    = tests/test_bun.c

.PHONY: all test sanitized test-sanitize test_valgrind clean

all: bun_parser

bun_parser: $(MAIN) $(LIB)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

# The test binary links the same source files, but not main.c (which has its
# own main()). libcheck provides the test runner's main() instead.
test: tests/test_runner
	./tests/test_runner

tests/test_runner: $(TEST) $(LIB)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $$(pkg-config --cflags --libs check)

# Sanitized builds: same as above, with ASan + UBSan added.
sanitized: bun_parser_sanitized

bun_parser_sanitized: $(MAIN) $(LIB)
	$(CC) $(CFLAGS) $(SANITIZE_FLAGS) $(LDFLAGS) -o $@ $^

# Sanitized tests: same as above, with ASan + UBSan added.
test-sanitize: bun_parser_sanitized tests/test_runner_sanitized
	./tests/test_runner_sanitized

tests/test_runner_sanitized: $(TEST) $(LIB)
	$(CC) $(CFLAGS) $(SANITIZE_FLAGS) $(LDFLAGS) -o $@ $^ $$(pkg-config --cflags --libs check)

# Valgrind: run the plain test runner under Valgrind Memcheck.
test-valgrind: tests/test_runner
	$(VALGRIND) $(VALGRIND_FLAGS) ./tests/test_runner

clean:
	rm -f bun_parser tests/test_runner bun_parser_sanitized tests/test_runner_sanitized *.o
