vpath %.c ../../src

LDLIBS = -lm -lSDL2
CFLAGS += -Wall -I. -I../../src -g

SRCS = test.c shaders.c frame_buffer.c fast_hsv2rgb_32bit.c palette.c val2pwm.c

all: test

# Native build using CLANG
test: $(SRCS)
	clang $(CFLAGS) $^ $(LDLIBS) -o $@

clean:
	rm -rf test
