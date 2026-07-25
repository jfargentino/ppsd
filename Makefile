DEBUG ?= 1

# toolchain and flags definitions #############################################
include mk/arch.mk
include mk/cflags.mk
include mk/ldflags.mk

CPPFLAGS+=-D_GNU_SOURCE -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3

ifeq ($(DEBUG), 1)
	CFLAGS += -g3 -UNDEBUG
else
	CFLAGS += -Os -DNDEBUG
endif

# Build targets ###############################################################
TARGETS=ppsd timeref adjtimex pps_stats
TESTS=timespec

#all: $(TARGETS) check
all: $(TARGETS) $(TESTS)

# Sources files ###############################################################
SRCS+=adjtimex_helper.c
SRCS+=pps_helper.c
SRCS+=pps_stats.c
SRCS+=ppsd.c
SRCS+=timeref.c
SRCS+=timespec_helper.c

# application #################################################################
HDRS=$(patsubst %.o, %.h, $(SRCS))
OBJS=$(addprefix build/, $(patsubst %.c, %.o, $(SRCS)))
GCNO=$(patsubst %.o, %.gcno, $(OBJS))
SU=$(patsubst %.o, %.su, $(OBJS))

mk_version.sh:
	ln -s mk_version_hash.sh $@

version.h: mk_version.sh $(SRCS) $(HDRS)
	./mk_version.sh > $@

ppsd: $(OBJS) build/ppsd_main.o
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ $(LDFLAGS) -lgps -o $@

timespec: timespec_helper.c
	$(CC) $(CFLAGS) -DTIMESPEC_HELPER_MAIN $(CPPFLAGS) $^ $(LDFLAGS) -o $@

pps_stats: pps_stats.c build/timespec_helper.o
	$(CC) $(CFLAGS) -DPPS_STATS_MAIN $(CPPFLAGS) $^ $(LDFLAGS) -lm -o $@

timeref: timeref.c build/adjtimex_helper.o build/timespec_helper.o
	$(CC) $(CFLAGS) -DTIMEREF_MAIN $(CPPFLAGS) $^ $(LDFLAGS) -lgps -o $@

adjtimex: adjtimex_helper.c build/timespec_helper.o
	$(CC) $(CFLAGS) -DADJTIMEX_HELPER_MAIN $(CPPFLAGS) $^ $(LDFLAGS) -o $@

hardpps: hardpps.o build/adjtimex_helper.o build/pps_helper.o
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ $(LDFLAGS) -o $@

build/%.o: %.c %.h slog.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@
 
build/%.o: %.c slog.h version.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

#.PHONY: version.h

# static analysis #############################################################
include mk/cppcheck.mk

STAT=$(patsubst %.o, %.misra, $(OBJS))

check: $(STAT) build/cppcheck.log

build/%.misra: %.c
	@echo ">>>>>>>> MISRA analysis on $< <<<<<<<"
	@cppcheck $(CPPCHECK_FLAGS) --addon=$(MISRA_ADDON) $< 2>&1 | tee $@

build/cppcheck.log: $(SRCS)
	@echo ">>>>>>>> CPPCHECK on all sources <<<<<<<"
	@cppcheck $(CPPCHECK_FLAGS) $^ 2>&1 | tee $@

# Cleaning ####################################################################
clean:
	@rm -vf hardpps.o version.h $(OBJS) $(GCNO) *.o *.gcno *.gcda

purge: clean
	@rm -vf $(TARGETS) $(TESTS) $(STAT) build/* *.su *.cppcheck gmon.out
