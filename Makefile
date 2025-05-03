# ------ Build configuration ------
CC              := gcc
CFLAGS          := -std=c11 -Wall -Werror -Wextra -pedantic -g
LDLIBS 			+= -Llibraries/llhttp -lllhttp\
                   -Llibraries/cjson -lcjson

INCDIRS 		:= inc browser/inc libraries/llhttp libraries/cjson
SRCDIRS         := src browser/src
BUILDDIR        := build
OBJDIR          := $(BUILDDIR)/obj
BINDIR          := $(BUILDDIR)/bin
TARGET          := server

# Expand include flags
INCLUDES        := $(foreach dir,$(INCDIRS),-I$(dir))

# ------ Source & object lists ------
SOURCES         := $(foreach dir,$(SRCDIRS),$(wildcard $(dir)/*.c))
OBJECTS         := $(patsubst %.c,$(OBJDIR)/%.o,$(SOURCES))
DEPS            := $(OBJECTS:.o=.d)

# ------ Phony targets ------
.PHONY: all clean run debug release tidy lint format

# ------ Default build ------
all: $(BINDIR)/$(TARGET)

# Link step ---------------------------------------------------------------
$(BINDIR)/$(TARGET): $(OBJECTS)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

# Compile step -------------------------------------------------------------
$(OBJDIR)/%.o: %.c | $(OBJDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

# Create the object directory only when needed-----------------------------
$(OBJDIR):
	@mkdir -p $(OBJDIR)

# Convenience helpers ------------------------------------------------------
run: all
	@./$(BINDIR)/$(TARGET)

debug: export CFLAGS += -O0
debug: all

release: export CFLAGS += -O2 -DNDEBUG
release: all

# Auto formatting ---------------------------------------------------------
format:
	find . -regex '.*\.\(c\|h\)' -exec clang-format -i {} +

# Static analysis ----------------------------------------------------------
lint:
	cppcheck --enable=all --inconclusive --std=c99 --language=c --quiet \
		--suppress=missingIncludeSystem \
		-Iinc -Ibrowser/inc -Ilibraries/cjson -Ilibraries/llhttp \
		src/ browser/src/

# Better Static analysis
tidy:
	@echo "🐻 Generating compile_commands.json using bear..."
	bear -- make -B > /dev/null

	@echo "🧠 Running clang-tidy (suppressing C11 unsafe API warnings)..."
	clang-tidy \
		-checks=-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling \
		src/*.c browser/src/*.c -p . -- \
		-Iinc -Ibrowser/inc -Ilibraries/cjson -Ilibraries/llhttp

	@echo "🧼 Cleaning temporary build files..."

# rm -f compile_commands.json // keep for now the compile_commands
	rm -f *.o */*.o */*/*.o

clean:
	rm -rf $(BUILDDIR) & rm server.log

# Auto‑generated dependency files -----------------------------------------
-include $(DEPS)
