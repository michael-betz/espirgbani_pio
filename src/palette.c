#include "palette.h"
#include "esp_log.h"
#include "common.h"
#include <stdint.h>
#include <stdlib.h>

static const char *T = "PALETTE";
extern const unsigned g_palettes[][P_SIZE];

const unsigned g_palettes[][P_SIZE] = {
	{// 0: hot
	 0xff000000, 0xff00001f, 0xff000034, 0xff000049, 0xff000061, 0xff000076,
	 0xff00008b, 0xff0000a0, 0xff0000b7, 0xff0000cc, 0xff0000e1, 0xff0000f6,
	 0xff000fff, 0xff0024ff, 0xff0039ff, 0xff004eff, 0xff0066ff, 0xff007bff,
	 0xff0090ff, 0xff00a5ff, 0xff00bcff, 0xff00d1ff, 0xff00e6ff, 0xff00fbff,
	 0xff1effff, 0xff3effff, 0xff5dffff, 0xff7dffff, 0xffa0ffff, 0xffbfffff,
	 0xffdfffff, 0xffffffff},
	{// 1: gnuplot2
	 0xff000000, 0xff200000, 0xff400000, 0xff600000, 0xff830000, 0xffa30000,
	 0xffc30000, 0xffe30000, 0xffff0007, 0xffff0020, 0xffff0039, 0xffff0052,
	 0xffff006e, 0xffff0087, 0xffef0fa0, 0xffdf1fb9, 0xffcd31d5, 0xffbd41ee,
	 0xffad51ff, 0xff9d61ff, 0xff8b73ff, 0xff7b83ff, 0xff6b93ff, 0xff5ba3ff,
	 0xff49b5ff, 0xff39c5ff, 0xff29d5ff, 0xff19e5ff, 0xff07f7ff, 0xff36ffff,
	 0xff9affff, 0xffffffff},
	{// 2: cubehelix
	 0xff000000, 0xff0d040c, 0xff1d0b14, 0xff2d1319, 0xff3c1f1a, 0xff472b18,
	 0xff4d3916, 0xff4e4714, 0xff4a5616, 0xff43621b, 0xff3b6c26, 0xff347335,
	 0xff2f784b, 0xff2f7a62, 0xff347a7b, 0xff407993, 0xff5378ab, 0xff6979bd,
	 0xff827bca, 0xff9c80d1, 0xffb889d4, 0xffcd93d2, 0xffdea0cd, 0xffeaaec8,
	 0xfff1bfc3, 0xfff3cdc1, 0xfff2dac3, 0xfff0e5c8, 0xffeeefd4, 0xfff0f5e1,
	 0xfff5faf0, 0xffffffff},
	{// 3: afmhot
	 0xff000000, 0xff000010, 0xff000020, 0xff000030, 0xff000041, 0xff000051,
	 0xff000061, 0xff000071, 0xff000483, 0xff001493, 0xff0024a3, 0xff0034b3,
	 0xff0046c6, 0xff0056d6, 0xff0066e6, 0xff0076f6, 0xff0888ff, 0xff1998ff,
	 0xff28a8ff, 0xff39b8ff, 0xff4bcaff, 0xff5bdaff, 0xff6beaff, 0xff7bfaff,
	 0xff8dffff, 0xff9dffff, 0xffadffff, 0xffbdffff, 0xffcfffff, 0xffdfffff,
	 0xffefffff, 0xffffffff},
	{// 4: giststern
	 0xff000000, 0xff100892, 0xff2010f4, 0xff3018cc, 0xff4120a0, 0xff512878,
	 0xff613050, 0xff713828, 0xff834141, 0xff934949, 0xffa35151, 0xffb35959,
	 0xffc66363, 0xffd66b6b, 0xffe67373, 0xfff67b7b, 0xffeb8383, 0xffc98c8c,
	 0xffa79393, 0xff859c9c, 0xff5fa5a5, 0xff3dadad, 0xff1bb5b5, 0xff05bdbd,
	 0xff27c6c5, 0xff46cece, 0xff64d6d6, 0xff82dede, 0xffa4e7e7, 0xffc2efef,
	 0xffe0f7f7, 0xffffffff},
};

#define N_PALETTES 5

const unsigned *get_palette(unsigned id)
{
	if (id >= N_PALETTES)
		id = N_PALETTES - 1;
	return g_palettes[id];
}

const unsigned *get_random_palette()
{
	unsigned val = RAND_AB(0, (N_PALETTES - 1));
	ESP_LOGD(T, "palette: %d", val);
	return g_palettes[val];
}
