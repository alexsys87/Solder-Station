# Host UI simulator: make -C tools/sim && tools/sim/sim <out_dir>
ROOT = ../..
SRC  = sim.c $(ROOT)/Core/Src/ui.c $(ROOT)/Core/Src/menu.c $(ROOT)/Core/Src/gfx.c \
       $(ROOT)/Core/Src/fonts_small.c $(ROOT)/Core/Src/fonts_digits.c $(ROOT)/Core/Src/settings.c
sim: $(SRC)
	$(CC) -std=c11 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast -Wno-format-truncation -I$(ROOT)/Core/Inc -o $@ $(SRC) -lm
clean:
	rm -f sim
