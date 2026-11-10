"""Prepare an isolated dormitory emission texture for the local drawing proof.

Copies the existing workspace artwork without repainting it; adds an upload
signature at the outer atlas edge. Never overwrites source art or HAK resources.
"""
import argparse
from pathlib import Path
from PIL import Image

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--art', type=Path, required=True)
    parser.add_argument('--material', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    texture = args.output / 'eom_draw_world.tga'
    material = args.output / args.material.name
    if texture.exists() or material.exists():
        raise FileExistsError('Choose a new output directory; existing resources are preserved')
    with Image.open(args.art) as source:
        if source.size != (2048,2048):
            raise ValueError('Expected the original 2048-square emission artwork')
        # Native test shows the screen mirrored horizontally. Correct real pixel
        # order at export rather than rely on a TGA horizontal-origin flag.
        image = source.convert('RGBA').transpose(Image.Transpose.FLIP_LEFT_RIGHT)
    for bottom in (False,True):
        for pixel in range(16):
            rgb = [(pixel*mul+offset+(77 if bottom else 0)+113) % 256
                   for mul,offset in zip((37,53,89),(19,71,139))]
            image.putpixel((pixel,2047 if bottom else 0),(*rgb,255))
    with texture.open('xb') as output:
        image.save(output,format='TGA',compression=None)
    original = args.material.read_text()
    expected = 'texture5 wt9a294ff7f9394c'
    if original.count(expected) != 1:
        raise ValueError('Unexpected material; no override was written')
    with material.open('x') as output:
        output.write('// LOCAL DRAWING PROBE: dedicated copy, not a production board binding.\n'+
                     original.replace(expected,'texture5 eom_draw_world'))
    print(texture)
    print(material)
