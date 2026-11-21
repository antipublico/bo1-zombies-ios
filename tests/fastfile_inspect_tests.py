"""Exercise the compiled inspector with synthetic fastfiles and damaged streams."""
import pathlib
import random
import struct
import subprocess
import sys
import tempfile
import zlib


def main():
    executable = pathlib.Path(sys.argv[1]).resolve()
    envelope = b"IWffu100" + struct.pack("<I", 473)
    xfile = struct.pack("<9I", 16, 4096, 8, 16, 32, 64, 128, 256, 512)
    payload = xfile + bytes(16)
    valid = envelope + zlib.compress(payload)
    # Cross the inflater's input and output buffer boundaries with deterministic data.
    generator = random.Random(473)
    large_header = struct.pack("<9I", 100000, 4096, 8, 16, 32, 64, 128, 256, 512)
    large_payload = large_header + bytes(16) + bytes(generator.getrandbits(8) for _ in range(99984))
    corrupt = bytearray(valid)
    corrupt[-1] ^= 1
    counts_header = struct.pack("<9I", 36, 4096, 8, 16, 32, 64, 128, 256, 512)
    valid_counts = counts_header + struct.pack("<4I", 1, 0xffffffff, 2, 0xffffffff) + bytes(20)
    def package(strings, tags, assets, block4=128, tail=b""):
        body = struct.pack("<4I", len(tags), 0xffffffff, len(assets), 0xffffffff)
        body += struct.pack("<" + "I" * len(tags), *tags) + strings
        body += b"".join(struct.pack("<II", *entry) for entry in assets) + tail
        header = struct.pack("<9I", len(body), 0, 0, 0, 0, 0, block4, 0, 0)
        return envelope + zlib.compress(header + body)

    alias = 0x80000000 + 8 + 1  # Block 4, first string after two pointer slots.
    cases = [
        ("valid", valid, True, "Inflated bytes 52"),
        ("large", envelope + zlib.compress(large_payload), True, "Inflated bytes 100036"),
        ("high_compression", envelope + zlib.compress(large_header + bytes(100000)), True, "Inflated bytes 100036"),
        ("asset_counts", envelope + zlib.compress(valid_counts), True, "Assets 2 (table present)"),
        ("size_mismatch", envelope + zlib.compress(xfile + bytes(17)), False, "Declared payload size"),
        ("negative_count", envelope + zlib.compress(xfile + struct.pack("<4I", 0xffffffff, 0, 0, 0)), False, "asset list header"),
        ("impossible_counts", envelope + zlib.compress(xfile + struct.pack("<4I", 0, 0, 0x7fffffff, 0xffffffff)), False, "Asset table counts exceed"),
        ("short_asset_list", envelope + zlib.compress(struct.pack("<9I", 15, 0, *([0] * 7)) + bytes(15)), False, "asset list header"),
        ("truncated_envelope", valid[:11], False, "Invalid fastfile header"),
        ("bad_magic", b"X" + valid[1:], False, "Invalid fastfile header"),
        ("old_version", valid[:8] + struct.pack("<I", 472) + valid[12:], False, "version 472"),
        ("authenticated", b"IWff0100" + valid[8:], False, "Authenticated fastfiles"),
        ("missing_stream", envelope, False, "truncated or unreadable"),
        ("truncated_stream", valid[:-1], False, "truncated or unreadable"),
        ("bad_checksum", corrupt, False, "Invalid compressed payload"),
        ("bad_deflate", envelope + b"invalid", False, "Invalid compressed payload"),
        ("short_xfile", envelope + zlib.compress(xfile[:35]), False, "XFile header is truncated"),
        ("inline_strings", package(b"a\0bc\0", [0, 0xffffffff, 0xffffffff], [(36, 0xffffffff), (37, 0)]), True, "Decoded script strings 2"),
        ("alias_string", package(b"hello\0", [0xffffffff, alias], [(36, 0)]), True, "Decoded script strings 2"),
        ("string_middle_alias", package(b"hello\0", [0xffffffff, alias + 1], []), False, "Unsupported script string reference"),
        ("forward_alias", package(b"hello\0", [alias, 0xffffffff], []), False, "Unsupported script string reference"),
        ("wrong_block_alias", package(b"hello\0", [0xffffffff, 0x60000009], []), False, "Unsupported script string reference"),
        ("bad_asset_type", package(b"", [], [(43, 0)]), False, "invalid type"),
        ("negative_asset_type", package(b"", [], [(0xffffffff, 0)]), False, "invalid type"),
        ("unterminated_string", package(b"abc", [0xffffffff], []), False, "Script string exceeds"),
        ("block_alignment_overflow", package(b"ab\0", [0xffffffff], [(36, 0)], block4=15), False, "Asset directory exceeds"),
        ("zero_padding", valid + bytes(31), True, "Inflated bytes 52"),
        ("excess_padding", valid + bytes(32), False, "trailing data"),
        ("trailing_junk", valid + b"x", False, "trailing data"),
        ("metadata_crosses_chunks", package(b"s" * 70000 + b"\0", [0xffffffff], [(36, 0)], block4=71000), True, "Decoded script strings 1"),
    ]
    with tempfile.TemporaryDirectory() as directory:
        for name, data, success, expected in cases:
            path = pathlib.Path(directory) / (name + ".ff")
            path.write_bytes(data)
            result = subprocess.run([str(executable), str(path)], capture_output=True, text=True, timeout=10)
            output = result.stdout + result.stderr
            if (result.returncode == 0) != success or expected not in output:
                raise RuntimeError(f"{name} failed ({result.returncode}): {output}")
        missing = subprocess.run([str(executable), str(pathlib.Path(directory) / "missing.ff")], capture_output=True)
        if missing.returncode == 0:
            raise RuntimeError("Missing file was accepted")
    print(f"Passed {len(cases) + 1} inspector cases.")


if __name__ == "__main__":
    main()
