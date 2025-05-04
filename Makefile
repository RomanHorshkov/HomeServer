# ------ Build configuration ------
CC              := gcc
CFLAGS          := -std=c11 -Wall -Werror -Wextra -pedantic -g
LDLIBS 			+= -Lexternal/llhttp -lllhttp\
                   -Lexternal/cjson -lcjson

INCDIRS 		:= include include/core include/browser external/llhttp external/cjson
SRCDIRS         := src
BUILDDIR        := build
OBJDIR          := $(BUILDDIR)/obj
BINDIR          := $(BUILDDIR)/bin
TARGET          := server

# Expand include flags
INCLUDES        := $(foreach dir,$(INCDIRS),-I$(dir))

# ------ Source & object lists ------
SOURCES 		:= $(shell find $(SRCDIRS) -name '*.c')
OBJECTS 		:= $(patsubst %.c,$(OBJDIR)/%.o,$(SOURCES))
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
$(OBJDIR)/%.o: %.c
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
ifndef FILES
	@echo "🛠  Formatting all .c/.h files in the project..."
	@find . -regex '.*\.\(c\|h\)' -exec clang-format -i {} +
else
	@echo "🛠  Formatting staged files: $(FILES)"
	@clang-format -i $(FILES)
endif

# Static analysis ----------------------------------------------------------
lint:
	@cppcheck --enable=all --inconclusive --std=c11 --language=c --quiet \
		--suppress=missingIncludeSystem \
		-Iinclude -Iinclude/core -Iinclude/browser \
		-Iexternal/cjson -Iexternal/llhttp \
		src/ \
		2> /dev/null

# Better Static analysis
tidy:
	@echo "🐻 Generating compile_commands.json using bear..."
	bear -- make -B > /dev/null

	@echo "🧠 Running clang-tidy (suppressing C11 unsafe API warnings)..."
	clang-tidy \
		-checks=-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling \
		$(shell find src -name '*.c') -p . -- \
		-Iinclude -Iinclude/core -Iinclude/browser \
		-Iexternal/cjson -Iexternal/llhttp

	@echo "🧼 Cleaning temporary build files..."
	rm -f *.o */*.o */*/*.o

# rm -f compile_commands.json // keep for now the compile_commands
	rm -f *.o */*.o */*/*.o

clean:
	rm -rf $(BUILDDIR) server.log

# Auto‑generated dependency files -----------------------------------------
-include $(DEPS)
