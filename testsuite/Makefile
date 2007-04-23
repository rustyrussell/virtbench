TESTS := $(patsubst %.c, %, $(wildcard testsuite/*.c))
check: $(TESTS)
	@set -e; for f in $(TESTS); do echo -n $$f...; $$f && echo "OK"; done

.INTERMEDIATE: $(TESTS)
$(TESTS): talloc.o stdrusty.o

testsuite/%: testsuite/%.c
