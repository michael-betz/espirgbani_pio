"""
Convert font files (.ttf, and others supported by freetype) to the internally used bitmap format (.fnt file).

Needs freetype-py installed.
"""
import re
import argparse
from pathlib import Path
from struct import pack, calcsize
import struct
from PIL import Image
from glob import glob
import freetype as ft


# typedef struct {
#     uint8_t width;  // bitmap width [pixels]
#     uint8_t height;  // bitmap height [pixels]
#     int8_t lsb;  // left side bearing
#     int8_t tsb;  // top side bearing
#     int8_t advance;  // cursor advance width
#     uint32_t start_index;  // offset to first byte of bitmap data (relative to start of glyph description table)
# } glyph_description_t;
FMT_GLYPH_DESCRIPTION = "BBbbbI"

# typedef struct {
#     uint16_t n_glyphs;  // number of glyphs in this font file
#     // simple ascci mapping parameters
#     uint16_t ascii_map_start = 32;  // the first glyph maps to this codepoint
#     uint16_t ascii_map_n = 96;  // how many glyphs map to incremental codepoints
#     // offset to optional glyph id to codepoint mapping table
#     uint16_t map_table_offset = glyph_description_offset;
#     uint32_t glyph_description_offset;
#     uint32_t glyph_data_offset;
#     uint16_t linespace;
#     int8_t yshift;  // to vertically center the digits, add this to tsb
#     // bit0: has_outline. glyph index of outline = glyph index of fill * 2
#     uint8_t flags;
#     const char[] name; // length = map_table_offset - sizeof(font_header_t)
# } font_header_t;
FMT_HEADER = "IHHHHIIHbB"

FLAG_HAS_OUTLINE = 1


def get_next_filename(out_dir):
    fnt_files = glob(str(out_dir / "*.fnt"))

    for i in range(1000):
        fname = out_dir / f"{i:03d}.fnt"
        if str(fname) not in fnt_files:
            return fname


def get_all_cps(face):
    all_cps = []
    all_cps.append(face.get_first_char()[0])

    while True:
        all_cps.append(face.get_next_char(all_cps[-1], 0)[0])
        if all_cps[-1] == 0:
            break
    return all_cps


def get_cp_set(args, face):
    """assemble the set of code-points to convert"""
    cp_set = set()

    if args.add_all:
        cp_set.update(get_all_cps(face))

    if args.add_range is not None:
        cp_set.update((int(x, base=0) for x in args.add_range.split(",")))

    if args.add_ascii:
        cp_set.update(range(0x20, 0x20 + 95))

    if args.add_numerals:
        cp_set.update([ord(x) for x in "1234567890:"])

    # We don't support ascii codes below 0x20
    cp_set.difference_update(range(0x20))

    return sorted(cp_set)


def get_n_ascii(cp_set):
    """how many characters correspond to the basic ascii scheme?"""
    first_char = 0
    for i, cp in enumerate(cp_set):
        if i == 0:
            first_char = cp
        elif first_char + i != cp:
            return (first_char, i)
    return (first_char, len(cp_set))


def get_glyph(code, face, is_outline=False):
    face.load_char(code, ft.FT_LOAD_RENDER)
    face.select_charmap(ft.FT_ENCODING_UNICODE)

    bitmap = face.glyph.bitmap
    width = face.glyph.bitmap.width
    rows = face.glyph.bitmap.rows

    props = {
        "width": width,
        "height": rows,
        "height_hr": face.glyph.metrics.height,
        "lsb": face.glyph.bitmap_left,
        "tsb": face.glyph.bitmap_top,
        "tsb_hr": face.glyph.metrics.horiBearingY,
        "advance": face.glyph.advance.x // 64,
    }
    return bytes(bitmap.buffer), props


def getImg(p, glyph_data_bs):
    start_ind = p["start_index"]
    end_ind = start_ind + p["width"] * p["height"]
    bs = glyph_data_bs[start_ind:end_ind]
    return Image.frombuffer("L", (p["width"], p["height"]), bs)


def get_preview(glyph_props, glyph_data_bs, yshift):
    print("\nGenerating preview image ...")
    total_advance = 0
    for p in glyph_props:
        total_advance += p["advance"]

    img_all = Image.new("L", (total_advance + 8, 32), 0x22)

    cur_x = 4
    for i, p in enumerate(glyph_props):
        img_all.paste(
            getImg(p, glyph_data_bs), (cur_x + p["lsb"], -p["tsb"] + 32 - yshift // 64)
        )
        cur_x += p["advance"]

    return img_all


def get_y_stats(glyph_props, DISPLAY_HEIGHT=32):
    """
    for the given list of glyph properties, returns their
      * maximum bounding box height (need to divide by 64)
      * y-shift needed to center the bounding box (need to divide by 64)
    """
    us = []
    ls = []
    for p in glyph_props:
        y_max = p["tsb_hr"]
        y_min = p["tsb_hr"] - p["height_hr"]
        # print(f'{y_max:3d}  {y_min:3d}')
        us.append(y_max)
        ls.append(y_min)

    bb_up = max(us)
    bb_down = min(ls)
    bb_mid = (bb_up + bb_down) / 2

    bb_height = bb_up - bb_down
    yshift = round(DISPLAY_HEIGHT / 2 * 64 - bb_mid)

    return bb_height, yshift


def auto_tune_font_size(face, target_height=30 * 64):
    """
    Find the correct char_size to fill the whole height of the display
    returns
      * char_size for face.set_char_size(height=char_size)
      * fractional yshift (needs to be divided by 64)
    """
    print("\nTuning font size ...")
    test_string = "1234567890:"
    char_size = target_height
    yshift = 0

    for i in range(8):
        face.set_char_size(height=char_size)
        props = [get_glyph(c, face)[1] for c in test_string]
        bb_height, yshift = get_y_stats(props)
        print(f"    {char_size / 64:4.1f}, {bb_height / 64:4.1f}, {yshift / 64:4.1f}")
        err = target_height - bb_height
        if err == 0:
            break
        char_size += err

    return char_size, yshift


def convert(args, face):
    print("\nGenerating bitmap .fnt file ...")
    # --------------------------------------------------
    #  Generate the glyph bitmap data
    # --------------------------------------------------
    glyph_data_bs = bytes()
    glyph_description_bs = bytes()
    glyph_props = []

    cp_set = get_cp_set(args, face)
    if len(cp_set) == 0:
        raise RuntimeError("No glyphs selected to convert")

    ascii_map_start, ascii_map_n = get_n_ascii(cp_set)

    for c in cp_set:
        print("    ", hex(c), chr(c))

        buf, props = get_glyph(chr(c), face)
        props["start_index"] = len(glyph_data_bs)

        # --------------------------------------------------
        #  Generate the glyph description table
        # --------------------------------------------------
        try:
            bs = pack(
                FMT_GLYPH_DESCRIPTION,
                props["width"],
                props["height"],
                props["lsb"],
                props["tsb"],
                props["advance"],
                props["start_index"],
            )
        except struct.error:
            print("Glyph doesnt fit. Skipping it...")
            continue

        glyph_props.append(props)
        glyph_data_bs += buf
        glyph_description_bs += bs

    print("    glyph_data:", len(glyph_data_bs), "bytes")
    print("    glyph_description_table:", len(glyph_description_bs), "bytes")

    # --------------------------------------------------
    #  Generate the codepoint to glyph mapping table
    # --------------------------------------------------
    # convert cp_set to uint32_t
    map_table_bs = b"".join(
        [cp.to_bytes(4, signed=False) for cp in cp_set[ascii_map_n:]]
    )
    print(f"    ascii_map_start: 0x{ascii_map_start:02x}, ascii_map_n: {ascii_map_n}")
    print(f"    map_table_bs: {len(map_table_bs)} bytes")

    # --------------------------------------------------
    #  Generate the font description header
    # --------------------------------------------------
    n_glyphs = len(glyph_props)

    name = face.family_name

    map_table_offset = calcsize(FMT_HEADER) + len(name) + 1
    glyph_description_offset = map_table_offset + len(map_table_bs)
    glyph_data_offset = glyph_description_offset + len(glyph_description_bs)

    linespace = face.size.height // 64
    yshift = get_y_stats(glyph_props)[1] // 64

    flags = 0

    font_header_bs = pack(
        FMT_HEADER,
        0x005A54BE,  # magic number
        n_glyphs,
        ascii_map_start,
        ascii_map_n,
        map_table_offset,
        glyph_description_offset,
        glyph_data_offset,
        linespace,
        yshift,
        flags,
    )

    out_name = Path("fnt/")
    out_name.mkdir(exist_ok=True)

    if args.numeric_name:
        out_name = get_next_filename(out_name)
    else:
        tmp = re.sub("[^A-Za-z0-9]+", "_", name.decode().lower())
        out_name = (out_name / tmp).with_suffix(".fnt")

    all_bs = (
        font_header_bs
        + name
        + b"\x00"
        + map_table_bs
        + glyph_description_bs
        + glyph_data_bs
    )

    with open(out_name, "wb") as f:
        f.write(all_bs)

    print(f"    wrote {out_name} ({len(all_bs) / 1024:.1f} kB)")

    return glyph_props, glyph_data_bs, out_name


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawTextHelpFormatter
    )
    parser.add_argument(
        "--font-height",
        default=30,
        type=float,
        help="Target height of the digits in [pixels]",
    )
    parser.add_argument(
        "--add-numerals",
        action="store_true",
        help="Add 0-9 and : to the generated file",
    )
    parser.add_argument(
        "--add-ascii",
        action="store_true",
        help="Add 95 Ascii characters to the generated file",
    )
    parser.add_argument(
        "--add-all", action="store_true", help="Add all glyphs in the font"
    )
    parser.add_argument(
        "--add-range", help="Add comma separated list of unicodes to the generated file"
    )

    parser.add_argument(
        "font_file", help="Name of the .ttf, .otf or .woff2 file to convert"
    )
    parser.add_argument(
        "--numeric-name",
        action="store_true",
        help="Use a numerical file name, which is incrementing with every file",
    )
    args = parser.parse_args()

    # Load an existing font file
    face = ft.Face(args.font_file)

    # determine its optimum size
    char_size, yshift = auto_tune_font_size(face, int(args.font_height * 64))

    # generate bitmap font
    face.set_char_size(height=char_size)
    glyph_props, glyph_data_bs, out_name = convert(args, face)

    # generate preview image
    img = get_preview(glyph_props, glyph_data_bs, yshift)
    out_name_png = out_name.with_suffix(".png")
    img.save(out_name_png, "PNG")
    print("    wrote", out_name_png)


if __name__ == "__main__":
    main()
