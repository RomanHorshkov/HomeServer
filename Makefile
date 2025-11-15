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

# --- libsodium detection (required by libdb.a) ---
ifeq ($(PKGCONFIG),)
	SODIUM_CFLAGS :=
	SODIUM_LIBS   := -lsodium
else
	SODIUM_CFLAGS := $(shell pkg-config --cflags libsodium 2>/dev/null)
	SODIUM_LIBS   := $(shell pkg-config --libs   libsodium 2>/dev/null)
	ifeq ($(SODIUM_LIBS),)
		SODIUM_LIBS := -lsodium
	endif
endif

# ------ Paths ------
APP_DIR   := app
SRCDIRS   := $(APP_DIR)/src $(APP_DIR)/external/SPSCring/app/src
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
	$(APP_DIR)/external/SPSCring/app/include

# External library search paths
LDFLAGS  += \
  -L$(APP_DIR)/external/llhttp \
  -L$(APP_DIR)/external/cjson \
  -L$(APP_DIR)/external/spsc_ring \
#   -L$(APP_DIR)/external/db

# App’s non-DB libs
LDLIBS_APP = -lllhttp -lcjson -lspsc_ring

# DB lib + its deps (order matters; group avoids --as-needed surprises)
LDLIBS_DB  = -Wl,--start-group $(DB_LIB) $(LMDB_LIBS) $(OPENSSL_LIBS) $(SODIUM_LIBS) -Wl,--end-group

LDLIBS = $(LDLIBS_APP) $(LDLIBS_DB)

# ------ Contract header (generated) ------
CONTRACT_MANIFEST := contract/manifest.json
CONTRACT_GEN      := utils/gen_contract_header.sh
CONTRACT_HDR      := $(APP_DIR)/include/contract_version.h

# ------ Flags ------
CPPFLAGS := $(addprefix -I,$(INCDIRS)) -MMD -MP $(OPENSSL_CFLAGS) $(LMDB_CFLAGS) $(SODIUM_CFLAGS) -D_POSIX_C_SOURCE=200809L
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
all: db-lib deps $(BINDIR)/$(TARGET)

# Warn early if deps missing (non-fatal: you might have a static lib that already bundles)
deps:
ifeq ($(LMDB_FOUND),0)
	@echo "[warn] LMDB not detected. Install: sudo apt-get install -y liblmdb-dev"
endif
ifeq ($(OPENSSL_FOUND),0)
	@echo "[warn] OpenSSL (libcrypto) not detected. Install: sudo apt-get install -y libssl-dev"
endif
ifeq ($(SODIUM_LIBS),-lsodium)
	@if ! printf '#include <sodium.h>\nint main(){return sodium_init();}\n' | $(CC) -x c - -o /dev/null -lsodium >/dev/null 2>&1; then \
		echo "[warn] libsodium not detected. Install: sudo apt-get install -y libsodium-dev"; \
	fi
endif

# Link ---------------------------------------------------------------
$(BINDIR)/$(TARGET): $(OBJECTS) $(DB_LIB)
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
	@find app \( -path 'app/external' -o -path 'app/external/*' \) -prune -o \
		-regex '.*\.(c\|h)' -exec clang-format -i {} +
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
	rm -rf $(BUILDDIR) \
		frontend/dist frontend/node_modules \
		HSFrontEnd/dist HSFrontEnd/node_modules \
		HSFrontEnd/HSFrontEnd/dist HSFrontEnd/HSFrontEnd/node_modules
	rm -f var/www/server.log var/www/map.json
	@$(MAKE) -C $(APP_DIR)/external/DataBase clean || true

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

# External Database project settings
DB_MODE ?= release
DB_DIR := $(APP_DIR)/external/DataBase
DB_BUILD_DIR := $(DB_DIR)
DB_INC := $(DB_DIR)/app/include
DB_LIB := $(DB_DIR)/build/$(DB_MODE)/lib/libdb.a
DB_UUID7_LIB := $(DB_DIR)/app/external/UUID7/build/libuuid7.a
DB_YYJSON_LIB := $(DB_DIR)/app/external/yyjson/libyyjson.a
DB_EMLOG_LIB := $(DB_DIR)/app/external/EMlog/libemlog.a
DB_EMLOG_INC := $(DB_DIR)/app/external/EMlog/app/include

# Additional static archives produced by the DB project that must be linked
DB_EXTRA_LIBS := $(DB_UUID7_LIB) $(DB_YYJSON_LIB) $(DB_EMLOG_LIB)

# Include directories exported by the DB project
DB_INC_DIRS := \
	$(DB_INC) \
	$(DB_INC)/auth \
	$(DB_INC)/client \
	$(DB_INC)/cryptography \
	$(DB_INC)/db \
	$(DB_INC)/db_app \
	$(DB_INC)/keys \
	$(DB_INC)/platform \
	$(DB_INC)/utils \
	$(DB_DIR)/app/external/UUID7/include
ifneq ($(wildcard $(DB_EMLOG_INC)/emlog.h),)
	DB_INC_DIRS += $(DB_EMLOG_INC)
endif

INCDIRS += $(DB_INC_DIRS)

# Ensure include path and library are used by the project
# Add DB include to CPPFLAGS and library to LDLIBS/LDFLAGS
CPPFLAGS += $(addprefix -I,$(DB_INC_DIRS))
# Link by direct path to static archive to avoid needing -L/-l adjustments:
LDLIBS += -pthread $(DB_EXTRA_LIBS)

# Build Database library before main targets:
# 'all' should depend on db library; if your existing all target differs, add db build as prerequisite.
.PHONY: db-lib
db-lib: $(DB_LIB)

$(DB_LIB):
	@echo "Building external Database project in $(DB_DIR) [MODE=$(DB_MODE)]"
	$(MAKE) -C $(DB_BUILD_DIR) MODE=$(DB_MODE) lib
	@if [ ! -f $(DB_LIB) ]; then \
		echo "ERROR: Database library $(DB_LIB) not found after building $(DB_BUILD_DIR)"; \
		exit 1; \
	fi
	@if [ -f $(DB_UUID7_LIB) ]; then :; else \
		echo "ERROR: Expected UUID7 static library $(DB_UUID7_LIB)"; \
		exit 1; \
	fi
	@if [ -f $(DB_YYJSON_LIB) ]; then :; else \
		echo "ERROR: Expected yyjson static library $(DB_YYJSON_LIB)"; \
		exit 1; \
	fi
	@if [ -f $(DB_EMLOG_LIB) ]; then :; else \
		echo "ERROR: Expected EMlog static library $(DB_EMLOG_LIB)"; \
		exit 1; \
	fi
