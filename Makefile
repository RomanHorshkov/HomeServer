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
.PHONY: all clean run debug release

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

clean:
	rm -rf $(BUILDDIR)

# Auto‑generated dependency files -----------------------------------------
-include $(DEPS)
