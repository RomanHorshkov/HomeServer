# ===== HomeServer — unified Makefile (server + external libdb.a) =====

# ------ Toolchain & warnings ------
CC       := gcc
CSTD     := -std=c11
WARN     := -Wall -Wextra -Werror -pedantic

# Build type flags
DBGFLAGS := -g -O0 -DDEBUG_MODE
RELFLAGS := -O2 -DNDEBUG

# ------ pkg-config detection ------
PKGCONFIG := $(shell command -v pkg-config 2>/dev/null)

# --- OpenSSL detection (required by libdb.a, directly or transitively) ---
ifeq ($(PKGCONFIG),)
  OPENSSL_CFLAGS :=
  OPENSSL_LIBS   := -lcrypto
  OPENSSL_FOUND  := $(shell printf 'int main(void){return 0;}\n' | \
                     $(CC) -x c - -o /dev/null -lcrypto >/dev/null 2>&1 && echo 1 || echo 0)
else
  OPENSSL_CFLAGS := $(shell pkg-config --cflags openssl 2>/dev/null)
  OPENSSL_LIBS   := $(shell pkg-config --libs   openssl 2>/dev/null)
  OPENSSL_FOUND  := $(shell pkg-config --exists openssl && echo 1 || echo 0)
endif

# --- LMDB detection (required by libdb.a) ---
ifeq ($(PKGCONFIG),)
  LMDB_CFLAGS :=
  LMDB_LIBS   := -llmdb
  LMDB_FOUND  := $(shell printf '#include <lmdb.h>\nint main(){mdb_env_create(0);return 0;}\n' | \
                   $(CC) -x c - -o /dev/null -llmdb >/dev/null 2>&1 && echo 1 || echo 0)
else
  LMDB_CFLAGS := $(shell pkg-config --cflags lmdb 2>/dev/null)
  LMDB_LIBS   := $(shell pkg-config --libs   lmdb 2>/dev/null)
  LMDB_FOUND  := $(shell pkg-config --exists lmdb && echo 1 || echo 0)
  ifeq ($(LMDB_LIBS),)
    LMDB_LIBS := -llmdb
    LMDB_FOUND := $(shell printf '#include <lmdb.h>\nint main(){mdb_env_create(0);return 0;}\n' | \
                     $(CC) -x c - -o /dev/null -llmdb >/dev/null 2>&1 && echo 1 || echo 0)
  endif
endif

# ------ Paths ------
APP_DIR   := app
SRCDIRS   := $(APP_DIR)/src
BUILDDIR  := build
OBJDIR    := $(BUILDDIR)/obj
BINDIR    := $(BUILDDIR)/bin
TARGET    := server

# External include roots (headers live here)
INCDIRS  := \
  $(APP_DIR)/include \
  $(APP_DIR)/include/core \
  $(APP_DIR)/include/browser \
  $(APP_DIR)/include/utils \
  $(APP_DIR)/include/browser/handlers \
  $(APP_DIR)/include/browser/router \
  $(APP_DIR)/external/llhttp \
  $(APP_DIR)/external/cjson \
  $(APP_DIR)/external/spsc_ring \
  $(APP_DIR)/external/db

# External library search paths
LDFLAGS  += \
  -L$(APP_DIR)/external/llhttp \
  -L$(APP_DIR)/external/cjson \
  -L$(APP_DIR)/external/spsc_ring \
  -L$(APP_DIR)/external/db

# App’s non-DB libs
LDLIBS_APP := -lllhttp -lcjson -lspsc_ring

# DB lib + its deps (order matters; group avoids --as-needed surprises)
LDLIBS_DB  := -Wl,--start-group -ldb $(LMDB_LIBS) $(OPENSSL_LIBS) -Wl,--end-group

LDLIBS := $(LDLIBS_APP) $(LDLIBS_DB)

# ------ Contract header (generated) ------
CONTRACT_MANIFEST := contract/manifest.json
CONTRACT_GEN      := utils/gen_contract_header.sh
CONTRACT_HDR      := $(APP_DIR)/include/contract_version.h

# ------ Flags ------
CPPFLAGS := $(addprefix -I,$(INCDIRS)) -MMD -MP $(OPENSSL_CFLAGS) $(LMDB_CFLAGS)
CFLAGS   := $(CSTD) $(WARN) $(DBGFLAGS)

# ------ Sources / objects / deps ------
SOURCES  := $(shell find $(SRCDIRS) -name '*.c')
OBJECTS  := $(patsubst %.c,$(OBJDIR)/%.o,$(SOURCES))
DEPS     := $(OBJECTS:.o=.d)

# ------ Phony ------
.PHONY: all clean run debug release tidy lint format notes help contract publish-contract deps

# ===== Contract: generate header from manifest =====
$(CONTRACT_HDR): $(CONTRACT_MANIFEST) $(CONTRACT_GEN)
	@bash $(CONTRACT_GEN)

# ------ Build targets ------
all: deps $(BINDIR)/$(TARGET)

# Warn early if deps missing (non-fatal: you might have a static lib that already bundles)
deps:
ifeq ($(LMDB_FOUND),0)
	@echo "[warn] LMDB not detected. Install: sudo apt-get install -y liblmdb-dev"
endif
ifeq ($(OPENSSL_FOUND),0)
	@echo "[warn] OpenSSL (libcrypto) not detected. Install: sudo apt-get install -y libssl-dev"
endif

# Link ---------------------------------------------------------------
$(BINDIR)/$(TARGET): $(OBJECTS)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@

# Compile -------------------------------------------------------------
# Ensure OBJDIR exists and CONTRACT_HDR is generated BEFORE compiling.
$(OBJDIR)/%.o: %.c | $(OBJDIR) $(CONTRACT_HDR)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(OBJDIR):
	@mkdir -p $(OBJDIR)

# ------ Convenience ------
debug: clean all
release: CFLAGS := $(CSTD) $(WARN) $(RELFLAGS)
release: clean all

run: all
	@./$(BINDIR)/$(TARGET) 3490

# Formatting ---------------------------------------------------------
format:
ifndef FILES
	@echo "🛠  Formatting all .c/.h files in the project..."
	@find app -regex '.*\.\(c\|h\)' -exec clang-format -i {} +
else
	@echo "🛠  Formatting staged files: $(FILES)"
	@clang-format -i $(FILES)
endif

# Static analysis ----------------------------------------------------
lint:
	@cppcheck --enable=all --inconclusive --std=c11 --language=c --quiet \
		--suppress=missingIncludeSystem \
		$(addprefix -I,$(INCDIRS)) \
		$(SRCDIRS)/

tidy:
	@echo "🐻 Generating compile_commands.json using bear…"
	bear -- make -B > /dev/null
	@echo "🧠 Running clang-tidy…"
	clang-tidy $(SOURCES) -p . -- $(addprefix -I,$(INCDIRS))
	@echo "🧼 Cleaning temporary build files…"
	rm -f *.o */*.o */*/*.o

# Build notes manifest ------------------------------------------------
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

# Contract publishing -------------------------------------------------
CONTRACT_PUB_DIR := /var/www/contract
publish-contract:
	@mkdir -p $(CONTRACT_PUB_DIR)
	@rsync -a --delete contract/ $(CONTRACT_PUB_DIR)/

# Clean ---------------------------------------------------------------
clean:
	rm -rf $(BUILDDIR) frontend/dist frontend/node_modules
	rm -f var/www/server.log var/www/map.json

# Auto-generated dependency files ------------------------------------
-include $(DEPS)

# Help ----------------------------------------------------------------
help:
	@echo "Targets:"
	@echo "  all            - build debug (default)"
	@echo "  run            - run ./build/bin/server 3490"
	@echo "  debug          - clean + build with DEBUG_MODE"
	@echo "  release        - clean + build optimized"
	@echo "  format         - clang-format C sources/headers"
	@echo "  lint           - cppcheck"
	@echo "  tidy           - bear + clang-tidy"
	@echo "  notes          - (re)generate build notes manifest"
	@echo "  publish-contract - copy ./contract → /var/www/contract"
	@echo "  clean          - remove build outputs"
