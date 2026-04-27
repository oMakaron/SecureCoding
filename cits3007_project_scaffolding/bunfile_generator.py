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
    # 07: Zero Assets (Valid according to spec, count is just 0)
    out = Path("tests/samples/valid/07-zero-assets.bun")
    with open(out, "wb") as f:
        write_header(f, 0, HEADER_SIZE, HEADER_SIZE, 0, HEADER_SIZE, 0)
    
    # 08: Unordered (Sections appear in non-canonical order)
    out = Path("tests/samples/valid/08-unordered.bun")
    with open(out, "wb") as f:
        d_off = HEADER_SIZE
        s_off = d_off + 4
        a_off = _align4(s_off + 5)
        write_header(f, 1, a_off, s_off, 5, d_off, 4)
        f.seek(d_off); f.write(b"DATA")
        f.seek(s_off); f.write(b"test\x00")
        f.seek(a_off); write_asset_record(f, 0, 5, 0, 4)

    # 09: Stress Test (Large valid file or complex structure)
    out = Path("tests/samples/valid/09-stress-test.bun")
    with open(out, "wb") as f:
        write_header(f, 1, HEADER_SIZE, HEADER_SIZE + RECORD_SIZE, 5, HEADER_SIZE + RECORD_SIZE + 8, 4)
        write_asset_record(f, 0, 5, 0, 4)
        f.seek(HEADER_SIZE + RECORD_SIZE); f.write(b"long\x00")
        f.seek(HEADER_SIZE + RECORD_SIZE + 8); f.write(b"DATA")

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
        write_asset_record(f, 0, 4, 0, 4) # Only writing one record

    # 20-27: Existing security/stress cases
    make_asset_count_max()
    make_size_overflow()
    make_offset_past_eof()
    make_offset_at_eof()
    make_duplicate_offsets()
    make_trunc_string()
    make_trunc_asset_record()
    make_non_null_terminated()

# --- Helper wrappers for 20-27 ---
def make_asset_count_max():
    with open("tests/samples/invalid/20-asset-count-max.bun", "wb") as f:
        write_header(f, 0xFFFFFFFF, HEADER_SIZE, HEADER_SIZE+4, 4, HEADER_SIZE+8, 4)

def make_size_overflow():
    with open("tests/samples/invalid/21-size-overflow.bun", "wb") as f:
        write_header(f, 1, HEADER_SIZE, HEADER_SIZE+RECORD_SIZE, 0xFFFFFFFFFFFFFFFF, HEADER_SIZE+RECORD_SIZE, 4)

def make_offset_past_eof():
    with open("tests/samples/invalid/22-offset-past-eof.bun", "wb") as f:
        write_header(f, 1, HEADER_SIZE, 200, 4, 300, 4)

def make_offset_at_eof():
    with open("tests/samples/invalid/23-offset-at-eof.bun", "wb") as f: # Spec says valid, but often tested as invalid logic
        write_header(f, 1, HEADER_SIZE, HEADER_SIZE+RECORD_SIZE, 0, HEADER_SIZE+RECORD_SIZE, 0)

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
        s_off = HEADER_SIZE+RECORD_SIZE
        write_header(f, 1, HEADER_SIZE, s_off, 6, s_off+8, 4)
        f.seek(HEADER_SIZE); write_asset_record(f, 0, 6, 0, 4)
        f.seek(s_off); f.write(b"DANGER")

def main():
    Path("tests/samples/valid").mkdir(parents=True, exist_ok=True)
    Path("tests/samples/invalid").mkdir(parents=True, exist_ok=True)
    make_valid_suite()
    make_invalid_suite()
    print("Success.")

if __name__ == "__main__":
    main()