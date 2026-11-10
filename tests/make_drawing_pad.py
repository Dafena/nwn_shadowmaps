"""Create the dedicated uncompressed 1024 RGBA diagnostic NUI texture (stdlib)."""
import argparse
import pathlib
import struct

def make():
    # Uncompressed true-color TGA; top-left origin, eight alpha bits. Tiny
    # opposite-row signatures identify this asset's native upload, not its name.
    data = bytearray(bytes((28,20,12,255))*1024**2)
    for bottom in (False, True):
        for pixel in range(16):
            rgb = [(pixel*mul+offset+(77 if bottom else 0)) % 256
                   for mul,offset in zip((37,53,89),(19,71,139))]
            start = ((1023 if bottom else 0)*1024+pixel)*4
            data[start:start+4] = bytes((*rgb[::-1],255))
    return struct.pack('<BBBHHBHHHHBB', 0, 0, 2, 0, 0, 0, 0, 0, 1024, 1024, 32, 40) + data

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=pathlib.Path)
    args = parser.parse_args()
    # Never silently overwrite an authored resource.
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('xb') as file:
        file.write(make())
