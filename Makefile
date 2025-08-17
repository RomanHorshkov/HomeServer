# ------ Build configuration ------
CC       := gcc
CSTD     := -std=c11
WARN     := -Wall -Wextra -Werror -pedantic

# Build type flags
DBGFLAGS := -g -O0 -DDEBUG_MODE
RELFLAGS := -O2 -DNDEBUG

# External libs
LDFLAGS  += -Lapp/external/llhttp -Lapp/external/cjson -Lapp/external/spsc_ring
LDLIBS   += -lllhttp -lcjson -lspsc_ring

# Include directories (relative to app/)
INCDIRS  := app/include app/include/core app/include/browser \
            app/include/utils app/include/browser/handlers \
            app/include/browser/router app/external/llhttp \
            app/external/cjson app/external/spsc_ring
SRCDIRS  := app/src
BUILDDIR := build
OBJDIR   := $(BUILDDIR)/obj
BINDIR   := $(BUILDDIR)/bin
TARGET   := server

# Contract header (generated)
CONTRACT_MANIFEST := contract/manifest.json
CONTRACT_GEN      := utils/gen_contract_header.sh
CONTRACT_HDR      := app/include/contract_version.h

# Include flags & default compile flags (debug by default)
CPPFLAGS := $(addprefix -I,$(INCDIRS)) -MMD -MP
CFLAGS   := $(CSTD) $(WARN) $(DBGFLAGS)

# Source/object/deps
SOURCES  := $(shell find $(SRCDIRS) -name '*.c')
OBJECTS  := $(patsubst %.c,$(OBJDIR)/%.o,$(SOURCES))
DEPS     := $(OBJECTS:.o=.d)

# ------ Phony targets ------
.PHONY: all clean run debug release tidy lint format notes help contract publish-contract


# ===== Contract: generate header from manifest =====
$(CONTRACT_HDR): $(CONTRACT_MANIFEST) $(CONTRACT_GEN)
	@bash $(CONTRACT_GEN)

# Link step ---------------------------------------------------------------
$(BINDIR)/$(TARGET): $(OBJECTS)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@

# Compile step -------------------------------------------------------------
# Ensure OBJDIR exists and CONTRACT_HDR is generated BEFORE compiling.
# It's an order-only dep here; subsequent rebuilds on manifest edits are handled
# by the auto-generated .d files because sources that include the header record it.
$(OBJDIR)/%.o: %.c | $(OBJDIR) $(CONTRACT_HDR)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(OBJDIR):
	@mkdir -p $(OBJDIR)

# ===== Top-level targets =====
# ------ Default build ------
all: $(BINDIR)/$(TARGET)

# Debug build (same as default)
debug: clean all

# Release build: optimized, no debug flags, no DEBUG_MODE
# Release overrides only CFLAGS; keeps includes/LDFLAGS etc.
release: CFLAGS := $(CSTD) $(WARN) $(RELFLAGS)
release: clean all

run: all
	@./$(BINDIR)/$(TARGET) 3490

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
		$(addprefix -I,$(INCDIRS)) \
		$(SRCDIRS)/

# Better Static analysis
tidy:
	@echo "🐻 Generating compile_commands.json using bear…"
	bear -- make -B > /dev/null

	@echo "🧠 Running clang-tidy (suppressing C11 unsafe API warnings)…"
	clang-tidy $(SOURCES) -p . -- $(addprefix -I,$(INCDIRS))

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



# ===== (Optional) publish the contract to static web dir =====
CONTRACT_PUB_DIR := /var/www/contract
publish-contract:
	@mkdir -p $(CONTRACT_PUB_DIR)
	@rsync -a --delete contract/ $(CONTRACT_PUB_DIR)/



# ─── Container ──────────────────────────────────────────────
# IMAGE_APP      ?= homeserver-app
# IMAGE_TAG      ?= $(shell git rev-parse --short HEAD)
# COMPOSE        ?= docker compose

# .PHONY: docker-build docker-push docker-up docker-down docker-clean

# docker-build:             ## Build multi‑arch images via compose
# 	$(COMPOSE) build

# docker-push:              ## Push images set in compose to registry
# 	$(COMPOSE) push

# docker-up:                ## Start (or upgrade) the stack in detached mode
# 	$(COMPOSE) up -d

# docker-down:              ## Stop & remove containers but keep the volume
# 	$(COMPOSE) down

# docker-clean:             ## Remove dangling layers & old images
# 	docker image prune -f

# ─── Clean up ─────────────────────────────────────────────
clean:
	rm -rf $(BUILDDIR) frontend/dist frontend/node_modules
	rm -f var/www/server.log var/www/map.json 

# Auto‑generated dependency files -----------------------------------------
-include $(DEPS)

# Help
help:
	@echo "Targets:"
	@echo "  all            - build debug (default)"
	@echo "  run            - run ./build/bin/server 3490"
	@echo "  debug          - clean + build with DEBUG_MODE"
	@echo "  release        - clean + build optimized, no DEBUG_MODE"
	@echo "  format         - clang-format C sources/headers"
	@echo "  lint           - cppcheck"
	@echo "  tidy           - bear + clang-tidy"
	@echo "  notes          - (re)generate build notes manifest"
	@echo "  publish-contract - copy ./contract → var/www/contract"
	@echo "  clean          - remove build outputs"
	
# Notes:
# - Source and include paths now use the 'app/' prefix for clarity.
# - Static files should be placed in 'var/www', persistent data in 'var/lib'.
# - This Makefile is standardized for maintainability and FHS alignment.
