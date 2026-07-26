# Auto-generate web_data.c from HTML source files in http_server/pages/.
# Included through EXTRA_TARGET_MAKEFILES so the rule exists during build.

TC1_WEB_DIR := $(SOURCE_ROOT)/TC1/http_server
TC1_PAGES_DIR := $(TC1_WEB_DIR)/pages
TC1_WEB_DATA_C := $(TC1_WEB_DIR)/web_data.c
TC1_GEN_WEB_DATA := $(SOURCE_ROOT)/TC1/tools/gen_web_data.py
TC1_HTML_FILES := $(wildcard $(TC1_PAGES_DIR)/*.html)
TC1_PYTHON3 ?= $(firstword $(wildcard /run/current-system/sw/bin/python3 /usr/bin/python3 /usr/local/bin/python3))

ifneq ($(strip $(TC1_HTML_FILES)),)
ifeq ($(strip $(TC1_PYTHON3)),)
$(error python3 not found; set TC1_PYTHON3=/path/to/python3)
endif

$(TC1_WEB_DATA_C): $(TC1_HTML_FILES) $(TC1_GEN_WEB_DATA)
	@echo "  GEN     $@"
	"$(TC1_PYTHON3)" "$(TC1_GEN_WEB_DATA)" "$(TC1_PAGES_DIR)" "$@"
endif
