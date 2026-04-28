#!/usr/bin/env python3
import struct
from pathlib import Path

# --- Constants & Formats ---
_HEADER_FMT = "<IHHIQQQQQQ"
_RECORD_FMT = "<IIQQQIIII"

BUN_MAGIC = 0x304E5542
HEADER_SIZE = struct.calcsize(_HEADER_FMT) # 60
RECORD_SIZE = struct.calcsize(_RECORD_FMT) # 48

def _align4(n): return (n + 3) & ~3

def write_header(f, asset_count, asset_table_offset, string_table_offset, 
                 string_table_size, data_section_offset, data_section_size):
    data = struct.pack(_HEADER_FMT, BUN_MAGIC, 1, 0, asset_count, 
                       asset_table_offset, string_table_offset, string_table_size, 
                       data_section_offset, data_section_size, 0)
    f.write(data)

def write_asset_record(f, name_off, name_len, data_off, data_size):
    data = struct.pack(_RECORD_FMT, name_off, name_len, data_off, data_size, 0, 0, 0, 0, 0)
    f.write(data)


# --- Generator Functions  ---
def make_valid_suite():

    out = Path("tests/samples/valid/07-zero-assets.bun")
    with open(out, "wb") as f:
        # 1. Configuration
        asset_count = 0
        asset_table_size = 0  # 0 * RECORD_SIZE
        
        str_data = b"empty\x00"
        str_size = _align4(len(str_data))
        
        # 2. Sequential Offset Stacking
        # Everything stays aligned to 4-byte boundaries
        a_off = _align4(HEADER_SIZE)

    
        s_off = _align4(a_off + asset_table_size)

        d_off = _align4(s_off + str_size)

        # 3. Write the Header
        # Note: data_section_size is 0 here just to make it a truly "empty" container
        write_header(f, asset_count, a_off, s_off, str_size, d_off, 0)

        # 4. Write String Table (since there are no assets to write)
        f.seek(s_off)
        f.write(str_data.ljust(str_size, b'\x00'))

    out = Path("tests/samples/valid/08-unordered.bun")
    with open(out, "wb") as f:
        d_off = HEADER_SIZE

        str_data = b"test\x00"
        str_size = _align4(len(str_data))  # → 8

        s_off = d_off + 4
        a_off = _align4(s_off + str_size)

        write_header(f, 1, a_off, s_off, str_size, d_off, 4)

        # Write data section
        f.seek(d_off)
        f.write(b"DATA")

        # Write string table
        f.seek(s_off)
        f.write(str_data)
        f.write(b"\x00" * (str_size - len(str_data)))  # padding

        # Write asset record
        f.seek(a_off)
        write_asset_record(f, 0, len(str_data) - 1, 0, 4)

    out = Path("tests/samples/valid/09-stress-test.bun")
    with open(out, "wb") as f:
        asset_count = 100

    # 1. String table
        str_data = b"stress_asset\x00"
        name_len = len(str_data) - 1  # exclude null
        str_size = _align4(len(str_data))

        # 2. Sizes
        asset_table_size = asset_count * RECORD_SIZE

        # 3. Layout
        a_off = _align4(HEADER_SIZE)
        s_off = _align4(a_off + asset_table_size)
        d_off = _align4(s_off + str_size)

        # Make data section big enough for ALL assets (better stress test)
        data_size = 4 * asset_count

        # 4. Header
        write_header(f, asset_count, a_off, s_off, str_size, d_off, data_size)

        # 5. Asset table
        f.seek(a_off)
        for i in range(asset_count):
            write_asset_record(
                f,
                0,
                name_len,
                i * 4,   # each asset has unique offset
                4
            )

        # 6. String table
        f.seek(s_off)
        f.write(str_data.ljust(str_size, b'\x00'))

        # 7. Data section
        f.seek(d_off)
        for i in range(asset_count):
            f.write(b"DATA")

 
    with open("tests/samples/valid/10-offset-at-eof.bun", "wb") as f:
        a_off = HEADER_SIZE
        s_off = HEADER_SIZE + RECORD_SIZE   # EOF after asset table
        d_off = s_off                       # same position

        # Header: string + data sections are size 0
        write_header(f, 1, a_off, s_off, 0, d_off, 0)

        
        f.seek(a_off)
        write_asset_record(f, 0, 0, 0, 0)

def make_invalid_suite():
    # 17: Misaligned Section Size (Size not multiple of 4)
    out = Path("tests/samples/invalid/17-misaligned-section-size.bun")
    with open(out, "wb") as f:
        write_header(f, 1, HEADER_SIZE, HEADER_SIZE+RECORD_SIZE, 3, HEADER_SIZE+RECORD_SIZE+4, 4)

    # 18: Header Contradiction (Overlapping sections)
    out = Path("tests/samples/invalid/18-header-contradiction.bun")
    with open(out, "wb") as f:
        # Asset table and string table claim the same start offset
        write_header(f, 1, HEADER_SIZE, HEADER_SIZE, 10, HEADER_SIZE + 20, 4)

    # 19: Asset Count Mismatch (Header says 2 assets, but file ends after 1)
    out = Path("tests/samples/invalid/19-asset-count-mismatch.bun")
    with open(out, "wb") as f:
        write_header(f, 2, HEADER_SIZE, HEADER_SIZE + (RECORD_SIZE * 2), 4, HEADER_SIZE + (RECORD_SIZE * 2) + 4, 4)
        str_data = b"test\x00"
        name_len = len(str_data) - 1
        write_asset_record(f, 0, name_len, 0, 4)

    # 20-27: Existing security/stress cases
    make_asset_count_max()
    make_size_overflow()
    make_offset_past_eof()
    make_duplicate_offsets()
    make_trunc_string()
    make_trunc_asset_record()
    make_non_null_terminated()

# --- Helper wrappers for 20-27 ---
def make_asset_count_max():
    with open("tests/samples/invalid/20-asset-count-max.bun", "wb") as f:
        write_header(
            f,
            0xFFFFFFFF,
            HEADER_SIZE,
            HEADER_SIZE + RECORD_SIZE,
            4,
            HEADER_SIZE + RECORD_SIZE + 4,
            4
        )

def make_size_overflow():
    with open("tests/samples/invalid/21-size-overflow.bun", "wb") as f:
        write_header(f, 1, HEADER_SIZE, HEADER_SIZE+RECORD_SIZE, 0xFFFFFFFFFFFFFFFF, HEADER_SIZE+RECORD_SIZE, 4)

def make_offset_past_eof():
    with open("tests/samples/invalid/22-offset-past-eof.bun", "wb") as f:
        write_header(f, 1, HEADER_SIZE, 200, 4, 300, 4)

        # Write minimal valid asset table so parser gets further
        f.seek(HEADER_SIZE)
        write_asset_record(f, 0, 4, 0, 4)

# MOVED INVALID 23 TO VALID 10

def make_duplicate_offsets():
    with open("tests/samples/invalid/24-duplicate-offsets.bun", "wb") as f:
        write_header(f, 2, HEADER_SIZE, 200, 4, 300, 4)
        f.seek(HEADER_SIZE); write_asset_record(f, 0, 4, 0, 4); write_asset_record(f, 0, 4, 0, 4)

def make_trunc_string():
    with open("tests/samples/invalid/25-trunc-string.bun", "wb") as f:
        write_header(f, 1, HEADER_SIZE, 108, 20, 150, 4)

def make_trunc_asset_record():
    with open("tests/samples/invalid/26-trunc-asset-record.bun", "wb") as f:
        write_header(f, 1, HEADER_SIZE, 108, 4, 150, 4)
        f.seek(HEADER_SIZE); f.write(b"short")

def make_non_null_terminated():
    with open("tests/samples/invalid/27-non-null-terminated.bun", "wb") as f:
        # Layout:
        # [Header][Asset Table][String Table][Data]

        a_off = HEADER_SIZE
        s_off = HEADER_SIZE + RECORD_SIZE

        str_data = b"DANG"          # only 4 bytes in string table
        str_size = 4                # MUST be aligned → 4 is OK

        d_off = s_off + str_size    # data section after string table

        # Header claims string table is ONLY 4 bytes
        write_header(f, 1, a_off, s_off, str_size, d_off, 4)

        # Asset record claims name is 6 bytes → OUT OF BOUNDS
        f.seek(a_off)
        write_asset_record(f, 0, 6, 0, 4)

        # Write actual string table (only 4 bytes exist)
        f.seek(s_off)
        f.write(str_data)

        # Write data section (just to keep file structurally complete)
        f.seek(d_off)
        f.write(b"DATA")

def main():
    Path("tests/samples/valid").mkdir(parents=True, exist_ok=True)
    Path("tests/samples/invalid").mkdir(parents=True, exist_ok=True)
    make_valid_suite()
    make_invalid_suite()
    print("Success.")

if __name__ == "__main__":
    main()