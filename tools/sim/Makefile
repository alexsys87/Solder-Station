# Host simulators
#   make -C tools/sim            UI screenshots (sim) and protocol test (proto_test)
#   tools/sim/sim <out_dir>      writes .pbm screenshots
#   tools/sim/proto_test         prints a protocol transcript
ROOT = ../..
CFLAGS = -std=c11 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast \
         -Wno-format-truncation
SIM_SRC = sim.c $(ROOT)/Core/Src/ui.c $(ROOT)/Core/Src/menu.c $(ROOT)/Core/Src/gfx.c \
          $(ROOT)/Core/Src/fonts_small.c $(ROOT)/Core/Src/fonts_digits.c $(ROOT)/Core/Src/settings.c
PROTO_SRC = proto_test.c $(ROOT)/Core/Src/proto.c $(ROOT)/Core/Src/settings.c

all: sim proto_test

sim: $(SIM_SRC)
	$(CC) $(CFLAGS) -I$(ROOT)/Core/Inc -o $@ $(SIM_SRC) -lm

proto_test: $(PROTO_SRC)
	$(CC) $(CFLAGS) -Ifake -I$(ROOT)/Core/Inc -o $@ $(PROTO_SRC) -lm

clean:
	rm -f sim proto_test

.PHONY: all clean
