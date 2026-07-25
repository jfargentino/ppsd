# Flags #######################################################################
# TODO check what is done by -Wall and -Wextra

WFLAGS := -Wall -Wextra -pedantic
WFLAGS += -Wshadow -Wundef -Wreturn-type -Wimplicit-fallthrough
WFLAGS += -fstrict-aliasing -Wstrict-aliasing
WFLAGS += -fstack-protector -Wstack-protector
WFLAGS += -Wstrict-overflow -Wconversion
WFLAGS += -Wbad-function-cast
WFLAGS += -Wcast-qual
WFLAGS += -Wmissing-prototypes -Wpacked
#WFLAGS += -Wpadded
WFLAGS += -Wwrite-strings -Wnested-externs
WFLAGS += -Wredundant-decls
WFLAGS += -Wswitch-default
#WFLAGS += -Wswitch-enum
WFLAGS += -Wstrict-prototypes -Wold-style-definition
WFLAGS += -Wtype-limits
WFLAGS += -Waggregate-return
WFLAGS += -Wshift-negative-value -Wreturn-type
WFLAGS += -Wmultichar -Wformat -Wformat=2 -Wformat-security -Wdouble-promotion
WFLAGS += -Wdeprecated -Wuninitialized
WFLAGS += -Wchar-subscripts
WFLAGS += -Wparentheses
WFLAGS += -Wfloat-equal

ifeq (clang, $(findstring clang,$(CC)))
#WFLAGS += -Xanalyzer
    WFLAGS += -Wliteral-conversion
    WFLAGS += -Wcast-align
	WFLAGS += -Wno-ignored-qualifiers
	WFLAGS += -Wstack-exhausted
else
    #WFLAGS += -fanalyzer
    #WFLAGS += -Wcast-align=strict
    WFLAGS += -Wcast-align
    WFLAGS += -Wduplicated-branches -Wduplicated-cond
    WFLAGS += -Wno-aggressive-loop-optimizations
	WFLAGS += -Wmaybe-uninitialized
	WFLAGS += -Wno-discarded-qualifiers
	WFLAGS += -Wstack-usage=2048
    WFLAGS += -Wunsuffixed-float-constants
    WFLAGS += -Wtrampolines
endif

# TODO DFLAGS and CFLAGS
CFLAGS += -std=c99
#CFLAGS += -Wc90-c99-compat
#CFLAGS += -Wtraditional
CFLAGS += -ffunction-sections -fdata-sections
CFLAGS += -fPIC
CFLAGS += $(WFLAGS)
#CFLAGS+=-fstack-clash-protection -fstack-protector-strong -fcf-protection=full

# instrumentation
ifeq (clang, $(findstring clang,$(CC)))
	CFLAGS += --coverage -fprofile-instr-generate -fcoverage-mapping
else
    CFLAGS += -fno-diagnostics-show-caret
	CFLAGS += -fstack-usage
#CFLAGS += --coverage -pg
endif

# Diagnostic format ###########################################################
CFLAGS += -fmessage-length=0
CFLAGS += -fdiagnostics-show-location=once
#CFLAGS += -fno-diagnostics-show-labels
#CFLAGS += -fdiagnostics-format=json

