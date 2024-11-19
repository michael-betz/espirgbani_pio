#include <stdio.h>
#include <stdint.h>

typedef struct {
	uint32_t magic;
    uint16_t n_glyphs;  // number of glyphs in this font file
    // simple ascci mapping parameters
    uint16_t map_start;  // the first glyph maps to this codepoint
    uint16_t map_n;  // how many glyphs map to incremental codepoints
    // offset to optional glyph id to codepoint mapping table
    uint16_t map_table_offset; // equal to glyph_description_offset when not present
    uint32_t glyph_description_offset;
    uint32_t glyph_data_offset;
    uint16_t linespace;
    int8_t yshift;  // to vertically center the digits, add this to tsb
    // bit0: has_outline. glyph index of outline = glyph index of fill * 2
    uint8_t flags;
} font_header_t;

typedef struct {
    uint8_t width;  // bitmap width [pixels]
    uint8_t height;  // bitmap height [pixels]
    int8_t lsb;  // left side bearing
    int8_t tsb;  // top side bearing
    int8_t advance;  // cursor advance width
    uint32_t start_index;  // offset to first byte of bitmap (relative to start of glyph description table)
} glyph_description_t;


FILE *fFnt = NULL;
font_header_t header;

void hd(uint8_t *buf, unsigned len)
{
    for (unsigned i = 0; i < len; i++) {
        if (i > 0 && i % 16 == 0)
            printf("\n");
        printf("%02x ", *buf++);
    }
    printf("\n");
}

void get_glyph_description(unsigned glyph_index, glyph_description_t *desc)
{
	if (glyph_index >= header.n_glyphs) {
		printf("invalid glyph index :( %d", glyph_index);
		return;
	}

	unsigned offs = header.glyph_description_offset;
	offs += glyph_index * sizeof(glyph_description_t);
	if (fseek(fFnt, offs, SEEK_SET) == -1) {
		perror("seek failed :(");
		return;
	}

	int n_read = fread(desc, sizeof(glyph_description_t), 1, fFnt);
	if (n_read != 1) {
		perror("read failed :(");
		return;
	}
}

void dump_bitmap(glyph_description_t *desc)
{
	uint8_t buff[4096];

	// Find the beginning and length of the glyph blob
	unsigned data_start = header.glyph_data_offset + desc->start_index;
	unsigned len = desc->width * desc->height;
	printf("len: %d\n", len);

	if (len > sizeof(buff)) {
		printf("overflow :(\n");
		return;
	}

	if (fseek(fFnt, data_start, SEEK_SET) == -1) {
		perror("seek failed :(");
		return;
	}

	int n_read = fread(buff, 1, len, fFnt);
	if (n_read != len) {
		perror(":(");
		return;
	}

	hd(buff, len);
}

void print_glyph_description(glyph_description_t *desc)
{
	printf("width: %d\n", desc->width);
	printf("height: %d\n", desc->height);
	printf("lsb: %d\n", desc->lsb);
	printf("tsb: %d\n", desc->tsb);
	printf("advance: %d\n", desc->advance);
	printf("start_index: %d\n", desc->start_index);
	dump_bitmap(desc);
	printf("\n");
}

int main(int argc, char* args[])
{
	if (argc < 2)
		return -1;
	printf("Hello world, reading %s\n", args[1]);
	printf("%d %d\n", sizeof(font_header_t), sizeof(glyph_description_t));

	fFnt = fopen(args[1], "r");

	int n_read = fread(&header, sizeof(header), 1, fFnt);
	if (n_read != 1) {
		perror(":(");
		return -1;
	}

	if (header.magic != 0x005A54BE) {
		printf("Wrong magic :( %x\n", header.magic);
		return -1;
	}

	printf("magic: %x\n", header.magic);
	printf("n_glyphs: %d\n", header.n_glyphs);
	printf("map_start: %d\n", header.map_start);
	printf("map_n: %d\n", header.map_n);
	printf("map_table_offset: %d\n", header.map_table_offset);
	printf("glyph_description_offset: %d\n", header.glyph_description_offset);
	printf("glyph_data_offset: %d\n", header.glyph_data_offset);
	printf("linespace: %d\n", header.linespace);
	printf("yshift: %d\n", header.yshift);
	printf("flags: %x\n", header.flags);

	char name[128] = {0};
	int name_len = header.map_table_offset - sizeof(font_header_t);
	if (name_len > sizeof(name) - 1) {
		printf("name too long :( %d\n", name_len);
		return -1;
	}

	n_read = fread(name, 1, name_len, fFnt);
	if (n_read != name_len) {
		perror(":(");
		return -1;
	}
	printf("name: %s, %d\n", name, name_len);
	printf("\n");

	for (unsigned i=0; i<header.n_glyphs; i+=1) {
		glyph_description_t desc;

		get_glyph_description(i, &desc);

		printf("glyph_index: %d\n", i);
		print_glyph_description(&desc);
	}

	return 0;
}
