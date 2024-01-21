Bin: ; mkdir -p $@
Depends: ; mkdir -p $@
TestResults: ; mkdir -p $@
include $(wildcard Depends/*.dep)

# assumes gcc/clang
WARNING_FLAGS=-Wall\
	-Wbad-function-cast\
	-Wextra \
	-Wvla\
	-Wmissing-noreturn\
	-Wcast-qual\
	-Wdeprecated\
	-Wdouble-promotion\
	-Werror=int-conversion\
	-Werror=implicit-int\
	-Werror=implicit-function-declaration\
	-Werror=incompatible-pointer-types\
	-Werror=unused-result\
	-Werror=switch\
	-Werror=format\
	-Werror=return-type\

# assumes clang
WARNING_FLAGS+=-Wassign-enum\
	-Wshadow \
	-Warray-bounds-pointer-arithmetic\
	-Wcovered-switch-default\
	-Wfor-loop-analysis\
	-Winfinite-recursion\
	-Wduplicate-enum\
	-Wmissing-field-initializers\
	-Werror=pointer-type-mismatch\
	-Werror=extra-tokens\
	-Werror=macro-redefined\
	-Werror=initializer-overrides\
	-Werror=sometimes-uninitialized\
	-Werror=unused-comparison\
	-Werror=undefined-internal\
	-Werror=non-literal-null-conversion\
	-Werror=nullable-to-nonnull-conversion\
	-Werror=nullability-completeness\
	-Werror=nullability\
	-Wuninitialized\
	-Wconditional-uninitialized\
	-Werror=undefined-internal\
	-Wcomma

SAN=-fsanitize=address,undefined,nullability
Bin/drmd_3: drmd_cli.c README.css | Bin Depends
	$(CC) $< -o $@ -O3 -MT $@ -MMD -MP -MF Depends/$<.3.dep $(WARNING_FLAGS)
Bin/drmd_1: drmd_cli.c README.css | Bin Depends
	$(CC) $< -o $@ -O1 -g -MT $@ -MMD -MP -MF Depends/$<.1.dep $(WARNING_FLAGS)
Bin/drmd_0: drmd_cli.c README.css | Bin Depends
	$(CC) $< -o $@ -O0 -g -MT $@ -MMD -MP -MF Depends/$<.0.dep $(WARNING_FLAGS)
Bin/drmd_0_san: drmd_cli.c README.css | Bin Depends
	$(CC) $< -o $@ -O0 -g -MT $@ -MMD -MP -MF Depends/$<.0_san.dep $(WARNING_FLAGS) $(SAN)
Bin/drmd: drmd_cli.c README.css | Bin Depends
	$(CC) $< -o $@ -O3 -MT $@ -MMD -MP -MF Depends/$<.dep $(WARNING_FLAGS)
Bin/TestDrMd_0: TestDrMd.c | Bin Depends
	$(CC) $< -o $@ -O0 -g -MT $@ -MMD -MP -MF Depends/$<.0.dep $(WARNING_FLAGS)
Bin/TestDrMd_1: TestDrMd.c | Bin Depends
	$(CC) $< -o $@ -O1 -g -MT $@ -MMD -MP -MF Depends/$<.1.dep $(WARNING_FLAGS)
Bin/TestDrMd_2: TestDrMd.c | Bin Depends
	$(CC) $< -o $@ -O2 -g -MT $@ -MMD -MP -MF Depends/$<.2.dep $(WARNING_FLAGS)
Bin/TestDrMd_3: TestDrMd.c | Bin Depends
	$(CC) $< -o $@ -O3 -g -MT $@ -MMD -MP -MF Depends/$<.3.dep $(WARNING_FLAGS)
Bin/TestDrMd_0_san: TestDrMd.c | Bin Depends
	$(CC) $< -o $@ -O0 -g -MT $@ -MMD -MP -MF Depends/$<.0_san.dep $(WARNING_FLAGS) $(SAN)
Bin/TestDrMd_1_san: TestDrMd.c | Bin Depends
	$(CC) $< -o $@ -O1 -g -MT $@ -MMD -MP -MF Depends/$<.1_san.dep $(WARNING_FLAGS) $(SAN)
Bin/TestDrMd_2_san: TestDrMd.c | Bin Depends
	$(CC) $< -o $@ -O2 -g -MT $@ -MMD -MP -MF Depends/$<.2_san.dep $(WARNING_FLAGS) $(SAN)
Bin/TestDrMd_3_san: TestDrMd.c | Bin Depends
	$(CC) $< -o $@ -O3 -g -MT $@ -MMD -MP -MF Depends/$<.3_san.dep $(WARNING_FLAGS) $(SAN)

.PHONY: tests
TestResults/%: Bin/Test% | TestResults
	$< --tee $@
tests: TestResults/DrMd_0
tests: TestResults/DrMd_1
tests: TestResults/DrMd_2
tests: TestResults/DrMd_3
tests: TestResults/DrMd_0_san
tests: TestResults/DrMd_1_san
tests: TestResults/DrMd_2_san
tests: TestResults/DrMd_3_san
all: tests

include Wasm/wasm.mak

Bin/drmd.wasm: drmd_wasm.c | Bin Depends
	$(WCC) $< -o $@ -MT $@ -MMD -MP -MF Depends/$<.dep $(WARNING_FLAGS) $(WASMCFLAGS)

.PHONY: exes
exes: Bin/drmd_3
exes: Bin/drmd_1
exes: Bin/drmd_0
exes: Bin/drmd_0_san
exes: Bin/drmd
all: exes

all: Bin/drmd.wasm

README.html: README.md README.css | Bin/drmd
	Bin/drmd README.md --stylesheet README.css -o $@

.PHONY: all
.DEFAULT_GOAL:=all
