# ------ Build configuration ------
CC              := gcc
CFLAGS          := -std=c11 -Wall -Werror -Wextra -pedantic -g

# External libraries (relative to app/external)
LDLIBS          += -Lapp/external/llhttp -lllhttp \
                   -Lapp/external/cjson -lcjson

# Include directories (relative to app/)
INCDIRS         := app/include app/include/core app/include/browser app/include/browser/handlers app/external/llhttp app/external/cjson
SRCDIRS         := app/src
BUILDDIR        := build
OBJDIR          := $(BUILDDIR)/obj
BINDIR          := $(BUILDDIR)/bin
TARGET          := server

# Expand include flags
INCLUDES        := $(foreach dir,$(INCDIRS),-I$(dir))

# For static analysis tools
CPPCHECK_INCLUDES := $(foreach dir,$(INCDIRS),-I$(dir))
CLANGTIDY_INCLUDES := $(CPPCHECK_INCLUDES)

# ------ Source & object lists ------
SOURCES         := $(shell find $(SRCDIRS) -name '*.c')
OBJECTS         := $(patsubst %.c,$(OBJDIR)/%.o,$(SOURCES))
DEPS            := $(OBJECTS:.o=.d)

# ------ Phony targets ------
.PHONY: all clean run debug release tidy lint format notes

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
	@find app -regex '.*\.\(c\|h\)' -exec clang-format -i {} +
else
	@echo "🛠  Formatting staged files: $(FILES)"
	@clang-format -i $(FILES)
endif

# Static analysis ----------------------------------------------------------
lint:
	@cppcheck --enable=all --inconclusive --std=c11 --language=c --quiet \
		--suppress=missingIncludeSystem \
		$(CPPCHECK_INCLUDES) \
		$(SRCDIRS)/

# Better Static analysis
tidy:
	@echo "🐻 Generating compile_commands.json using bear…"
	bear -- make -B > /dev/null

	@echo "🧠 Running clang-tidy (suppressing C11 unsafe API warnings)…"
	clang-tidy $(SOURCES) -p . -- $(CLANGTIDY_INCLUDES)

	@echo "🧼 Cleaning temporary build files…"
	rm -f *.o */*.o */*/*.o

# ─── Build a manifest of all .puml diagrams and .md/.txt notes ───
NOTES_DIR := var/www/build_notes/notes
DIAG_DIR  := var/www/build_notes/diagrams
MANIFEST  := var/www/build_notes/manifest.json

notes:
	@echo "Generating $(MANIFEST)…"
	@mkdir -p $(dir $(MANIFEST))
	@echo "{"                                                          >  $(MANIFEST)
	@echo '  "diagrams": ['                                           >> $(MANIFEST)
	@find $(DIAG_DIR) -maxdepth 1 -type f -name '*.puml' \
	  | sed -e 's@.*/\(.*\)$$@"\1"@' \
	  | paste -sd ",\n    " -                                        >> $(MANIFEST)
	@echo '  ],'                                                      >> $(MANIFEST)
	@echo '  "notes": ['                                              >> $(MANIFEST)
	@find $(NOTES_DIR) -maxdepth 1 -type f \( -name '*.md' -o -name '*.txt' \) \
	  | sed -e 's@.*/\(.*\)$$@"\1"@' \
	  | paste -sd ",\n    " -                                        >> $(MANIFEST)
	@echo '  ]'                                                       >> $(MANIFEST)
	@echo "}"                                                         >> $(MANIFEST)
	@echo "$(MANIFEST) updated."

# ─── Container ──────────────────────────────────────────────
IMAGE_APP      ?= homeserver-app
IMAGE_TAG      ?= $(shell git rev-parse --short HEAD)
COMPOSE        ?= docker compose

.PHONY: docker-build docker-push docker-up docker-down docker-clean

docker-build:             ## Build multi‑arch images via compose
	$(COMPOSE) build

docker-push:              ## Push images set in compose to registry
	$(COMPOSE) push

docker-up:                ## Start (or upgrade) the stack in detached mode
	$(COMPOSE) up -d

docker-down:              ## Stop & remove containers but keep the volume
	$(COMPOSE) down

docker-clean:             ## Remove dangling layers & old images
	docker image prune -f

# ─── Clean up ─────────────────────────────────────────────
clean:
	rm -rf $(BUILDDIR)
	rm var/www/server.log var/www/map.json 

# Auto‑generated dependency files -----------------------------------------
-include $(DEPS)

# Notes:
# - Source and include paths now use the 'app/' prefix for clarity.
# - Static files should be placed in 'var/www', persistent data in 'var/lib'.
# - This Makefile is standardized for maintainability and FHS alignment.
