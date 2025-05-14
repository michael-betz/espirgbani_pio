vpath %.c ../../src

LDLIBS = -lm -lSDL2
CFLAGS += -Wall -I. -I../../src -g

SRCS = test.c shaders.c frame_buffer.c fast_hsv2rgb_32bit.c palette.c val2pwm.c font.c

all: test

# Native build using CLANG
test: $(SRCS)
	clang $(CFLAGS) $^ $(LDLIBS) -o $@


# Web-assembly build. Run it locally with `emrun dist/index.html`
wasm: $(SRCS)
	mkdir -p dist && \
	emcc $(CFLAGS) --shell-file shell_minimal.html -s USE_SDL=2 -s TOTAL_MEMORY=32MB $^ $(LDLIBS) -o dist/index.html

clean:
	rm -rf test
