# PUUKKO – C64 edition. Needs cc65, python3 + numpy; c1541 (VICE) for the .d64.
CC65_TARGET = c64
DEFS   ?=
CFLAGS  = -t $(CC65_TARGET) -g -O -Or -Cl -I src -I build $(DEFS)
AFLAGS  = -t $(CC65_TARGET) -g $(DEFS)
SRC_C   = src/main.c src/gfx.c src/stages.c src/spr.c src/fight.c
SRC_S   = src/sys.s
BUILD   = build
OBJS    = $(SRC_C:src/%.c=$(BUILD)/%.o) $(SRC_S:src/%.s=$(BUILD)/%.o) $(BUILD)/gen_data.o
PRG     = $(BUILD)/puukko.prg
D64     = $(BUILD)/puukko.d64
WAVS    = $(wildcard ./audio/*.wav)

all: $(PRG) $(D64)

src/gen_data.h $(BUILD)/gen_data.s: tools/mkdata.py $(WAVS) | $(BUILD)
	python3 tools/mkdata.py ./audio $(BUILD)/gen_data.s src/gen_data.h

$(BUILD)/%.o: src/%.c src/puukko.h src/gen_data.h src/charmap.h | $(BUILD)
	cc65 $(CFLAGS) -o $(BUILD)/$*.s $<
	ca65 $(AFLAGS) -o $@ $(BUILD)/$*.s

$(BUILD)/sys.o: src/sys.s | $(BUILD)
	ca65 $(AFLAGS) -o $@ $<

$(BUILD)/gen_data.o: $(BUILD)/gen_data.s
	ca65 $(AFLAGS) -o $@ $<

$(PRG): $(OBJS) puukko.cfg
	ld65 -C puukko.cfg -m $(BUILD)/puukko.map -Ln $(BUILD)/puukko.lbl -o $@ $(OBJS) c64.lib

LOADER  = $(BUILD)/loader.prg
LOADSCR = $(wildcard loadscreen/*.seq)

$(LOADER): src/loader.s loader.cfg $(LOADSCR) | $(BUILD)
	ca65 -t $(CC65_TARGET) -I . -o $(BUILD)/loader.o src/loader.s
	ld65 -C loader.cfg -o $@ $(BUILD)/loader.o

# disk: "PUUKKO" = loading screen + loader (first file, LOAD"*",8 picks it),
#       "PUUKKO GAME" = the game itself
$(D64): $(PRG) $(LOADER)
	rm -f $@
	c1541 -format "puukko,pk" d64 $@ -write $(LOADER) puukko -write $(PRG) "puukko game" >/dev/null

$(BUILD):
	mkdir -p $(BUILD)

run: $(D64)
	x64sc -autostart $(D64)

clean:
	rm -rf $(BUILD) src/gen_data.h

.PHONY: all run clean
