###############################################################################
# "make demos" to build the demos, prog name is "demo_xyz" in the current dir.
# "make tests" to build the tests, prog name is 'test_xyz' in current dir.
# "make misra" for MISRA static check, results in "analysis" sub-dir
# "make cov" to run tests and report code covered
# "make dox" to build doxygen documentation
# "make reports" for per-file HTML report
#
# "build/*.su" files contain stack usage as reported by gcc "-fstack-usage"
# option.
# Compilation flags are all in "mk/cflags.mk".
#
# all dependencies are available on ubuntu 20.04 repo:
# - cccc
# - complexity
# - pmccabe
# - sloccount
# - cppcheck
# - splint
# - universal-ctags
# - doxygen
# - libcmocka-dev && libcmocka-dev:i386 for 32bit build
# - lcov
#
###############################################################################

DEBUG ?= 1

# toolchain and flags definitions #############################################
include mk/arch.mk
include mk/cflags.mk
include mk/ldflags.mk

ifeq ($(DEBUG), 1)
	CFLAGS += -g3 -UNDEBUG
else
	CFLAGS += -Os -DNDEBUG
endif

# Src files definition ########################################################
include mk/src.mk

# Generics building targets ###################################################
all: tests misra cov reports

everything: all dox

TREE = build/ build/Src/ build/cmocka/
TREE += analysis/ coverage/ report/ unit_test/ dox/

$(TREE):
	@mkdir -vp $@

OBJ = $(addprefix build/, $(patsubst %.c, %.o, $(SRC)))

obj: Inc/version.h build/Src/ $(OBJ)

# TODO svn/git revision for misc/audio_amp.doxy:PROJECT_NUMBER
Inc/version.h: $(SRCX) $(HEAD1)
	@echo ">>>>>>>> $@ <<<<<<<"
	@scripts/mk_version.sh $@

build/Src/%.o: Src/%.c Inc/%.h $(HEAD0)
	@echo ">>>>>>>> Building $< <<<<<<<"
	@$(CC) $(CFLAGS) -c -o $@ $< 2>&1 | tee $@.log

MOCKING_OBJ = $(addprefix build/, $(patsubst %.c, %.o, $(MOCKING_SRC)))

mocking_obj: build/cmocka/ $(MOCKING_OBJ)

build/cmocka/%.o: cmocka/%.c
	@echo ">>>>>>>> Building $< <<<<<<<"
	@$(CC) $(CFLAGS) -c -o $@ $< 2>&1 | tee $@.log

# Building demos: "make demos" ################################################
DEMOS = demo_filter
DEMOS += demo_iir3
DEMOS += demo_io_buff
DEMOS += demo_sinus_48kHz
DEMOS += demo_audio_xfer

DEMO_OBJ = $(addsuffix .o, $(addprefix build/cmocka/, $(DEMOS)))

demo_obj: build/cmocka/ $(DEMO_OBJ)

demos: obj mocking_obj demo_obj $(DEMOS)
	@echo ">>>>>>>> DEMO <<<<<<<"
	@./demo_sinus_48kHz 30 32000 \
		| ./demo_audio_xfer \
		| gnuplot -p -e "plot '-' with linespoints"

build/cmocka/demo_%.o: cmocka/demo_%.c
	@echo ">>>>>>>> Building demo obj $@ <<<<<<<"
	@$(CC) $(CFLAGS) -c -o $@ $< 2>&1 | tee $@.log

demo_%: $(OBJ) $(MOCKING_OBJ) build/cmocka/demo_%.o
	@echo ">>>>>>>> DEMO $@ <<<<<<<"
	@$(CC) $(CFLAGS) -o $@ $^

# generate data to test filter
sin_%Hz.txt: demo_sinus_48kHz
	@echo ">>>>>>>> DATA $@ <<<<<<<"
	@./$<  $(patsubst %Hz.txt, %, $(patsubst sin_%, %, $@)) 48000 > $@

# Apply a filter on a give sinus
filter_%Hz.txt: demo_filter sin_%Hz.txt
	@echo ">>>>>>>> FILTERING $@ <<<<<<<"
	@cat ./$(word 2, $^) | ./$< > $@
	@cat $@ | gnuplot -p -e "plot '-' with linespoints"

# Building tests: "make tests" #################################################
TESTS = test_ampli_automata
TESTS += test_ampli_command
TESTS += test_ampli_status
TESTS += test_debounced_input
TESTS += test_io_buff
TESTS += test_io_buff32
TESTS += test_link_automata
TESTS += test_main_loop
TESTS += test_wallclock_extended

TEST_OBJ = $(addsuffix .o, $(addprefix build/cmocka/, $(TESTS)))

test_obj: build/cmocka/ $(TEST_OBJ)

build/cmocka/test_%.o: cmocka/test_%.c Src/%.c
	@echo ">>>>>>>> Building test obj $@ <<<<<<<"
	@$(CC) $(CFLAGS) -c -o $@ $< 2>&1 | tee $@.log

tests: obj mocking_obj test_obj $(TESTS)

cmocka/test_io_buff32.c: cmocka/test_io_buff.c Src/io_buff.c
	@sed 's/int16_t/int32_t/g' $< > $@
	@sed -i 's/_init16/_init32/g' $@
	@sed -i 's/_ptr16/_ptr32/g' $@

test_io_buff32: % : build/Src/io_buff.o build/cmocka/%.o
	@echo ">>>>>>>> Building TEST $@ <<<<<<<"
	@$(CC) $(CFLAGS) -o $@ $^ -lcmocka

test_%: $(OBJ) $(MOCKING_OBJ) build/cmocka/test_%.o
	@echo ">>>>>>>> Building TEST $@ <<<<<<<"
	@$(CC) $(CFLAGS) -o $@ $^ -lcmocka

# Running tests for code coverage ##############################################
# "make unit_test/test_XYZ.tap" run test_XYZ and record tests results
# "make unit_test/test_XYZ.xml" run test_XYZ and record tests results in XML
# "make unit_test/test_XYZ.txt" convert test_XYZ in a human-readable format
# "make unit_test/index.html" do lcov analysis and its HTML report

TAPS= $(addprefix unit_test/, $(addsuffix .tap, $(TESTS)))
TXML= $(addprefix unit_test/, $(addsuffix .xml, $(TESTS)))
TTXT= $(addprefix unit_test/, $(addsuffix .txt, $(TESTS)))

cov: tests demos coverage/index.html $(TTXT) $(TXML)

unit_test/test_%.tap: test_% unit_test/
	@echo ">>>>>>>> RUN $< <<<<<<<"
	@env CMOCKA_MESSAGE_OUTPUT=TAP ./$< | tee $@
	@mv gmon.out coverage/$<.gmon.out

unit_test/test_%.txt: cmocka/test_%.c unit_test/
	@echo ">>>>>>>> converting $< in $@ <<<<<<<"
	@scripts/test2txt.sh $< | tee $@

unit_test/test_%.xml: test_%  unit_test/
	@echo ">>>>>>>> RUN $< <<<<<<<"
	@env CMOCKA_MESSAGE_OUTPUT=XML ./$< | tee $@

# TODO running demos to have filter/iir3 coverage ?
coverage/tests_results.info: coverage/ $(TAPS)
	@echo ">>>>>>>> LCOV $@ <<<<<<<"
	@lcov --capture --directory build/ --output-file $@

# TODO genhtml -d option for tests description
coverage/index.html: coverage/tests_results.info coverage/
	@echo ">>>>>>>> coverage report $@ <<<<<<<"
	@genhtml -s -k -t "MF19 - Audio Amplifier" $< --o $(word 2, $^)

# OPTIONAL
coverage/%.gcov: build/Src/%.o Src/%.c
	@echo ">>>>>>>> GCOV $< <<<<<<<"
	@gcov $<
	@mv $(notdir $(word 2, $^)).gcov $@

# OPTIONAL
coverage/test_%.gcov: build/cmocka/test_%.o cmocka/test_%.c
	@echo ">>>>>>>> GCOV $< <<<<<<<"
	@gcov $<
	@mv $(notdir $(word 2, $^)).gcov $@

# Static analysis ##############################################################
# "make misra" for a per-file MISRA static analysis
MISRA = $(addprefix analysis/, $(patsubst %.c, %.misra.log, $(SRC0) $(SRC1)))

misra: analysis/ analysis/all_src.cppcheck.log $(MISRA)

include mk/cppcheck.mk

analysis/%.misra.log: Src/%.c
	@echo ">>>>>>>> MISRA analysis on $< <<<<<<<"
	@cppcheck $(CPPCHECK_FLAGS) --addon=$(MISRA_ADDON) $< 2>&1 | tee $@

analysis/all_src.cppcheck.log: $(SRCX)
	@echo ">>>>>>>> CPPCHECK on all sources <<<<<<<"
	@cppcheck $(CPPCHECK_FLAGS) Src 2>&1 | tee $@

# Per file report #############################################################
HTML=$(addprefix report/, $(patsubst %.c, %.html, $(SRC0) $(SRC1)))

reports: misra cov report/index.html

compiler_ver:
	@$(CC) --version | head -n 1

compiler_flags:
	@echo "$(CFLAGS)"

cppcheck_ver:
	@cppcheck --version

cppcheck_flags:
	@echo "$(CPPCHECK_FLAGS)"

report/index.html: $(HTML)
	@./scripts/mk_full_report.sh > $@

report/%.html: Src/%.c report/
	@./scripts/mk_html_report.sh $< > $@

# documentation ###############################################################
tags: cmocka/main.c $(SRCX) $(HEAD)
	@echo ">>>>>>>> TAGS <<<<<<<"
	@ctags $^

misc/dbg_cfg.doxy: $(SRCX) $(HEAD)
	@echo "/** @page dbg_cfg_list Debug options list" | tee $@
	@grep -rn dbg_cfg Src/ Inc/ \
    | sed 's/^.*\\dbg_cfg/\n\\dbg_cfg/' | sed 's/\*\/.*//' | tee -a $@
	@echo "\n */" | tee -a $@

misc/app_cfg.doxy: $(SRCX) $(HEAD)
	@echo "/** @page app_cfg_list Application configurations list" | tee $@
	@grep -rn app_cfg Src/ Inc/ \
    | sed 's/^.*\\app_cfg/\n\\app_cfg/' | sed 's/\*\/.*//' | tee -a $@
	@echo "\n */" | tee -a $@

misc/requirements.doxy: misc/sacl_requirements.txt
	@echo "/** @page Requirements Requirements list" | tee $@
	@sed 's/^\[\(MF19-AMPAUDIO-SACL-[0-9]\{6\}\)] \(.*\)/@anchor \1 [\1\] \2\n/' $^ | tee -a $@
	@echo " */" | tee -a $@

DOXY=misc/dbg_cfg.doxy misc/app_cfg.doxy misc/requirements.doxy

dox: misc/audio_amp.doxy $(SRCX) $(HEAD) cmocka/*.[ch] README.md $(DOXY)
	@echo ">>>>>>>> DOXYGEN <<<<<<<"
	@doxygen $<

# CLEANING ####################################################################

clean:
	-@rm -vf $(OBJ) $(MOCKING_OBJ) build/*/*.gcno build/*/*.gcda
	-@rm -vf $(TEST_OBJ) $(DEMO_OBJ) cmocka/test_io_buff32.c

distclean: clean
	-@rm -vf $(TESTS) $(DEMOS) $(MISRA) $(TTXT) $(TAPS)
	-@rm -vf *.[ch].gcov *.gmon.out

purge: distclean
	-@rm -rf $(TREE)

#.PHONY: obj
