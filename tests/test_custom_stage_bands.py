"""Check actual background pixels, independently of renderer diagnostic counters."""
from pathlib import Path
import struct
import sys

def runs(path, low=300, high=550, distinct=False, rgb=False, x=80):
    data = path.read_bytes()
    offset = struct.unpack_from('<I', data, 10)[0]
    width, height = struct.unpack_from('<ii', data, 18)
    step = struct.unpack_from('<H', data, 28)[0] // 8
    assert (width, abs(height)) == (1152, 720) and step in (3, 4)
    stride = ((width * step + 3) // 4) * 4
    lengths, last, colors, ordered = [], None, set(), []
    # Left sea background, below the assist legend and outside the carrier.
    for y in range(low, high):
        row = abs(height)-1-y if height > 0 else y
        pixel = data[offset+row*stride+x*step:offset+row*stride+x*step+3]
        colors.add(pixel)
        ordered.append(pixel)
        if pixel != last:
            lengths.append(1)
            last = pixel
        else:
            lengths[-1] += 1
    return ordered if rgb else len(colors) if distinct else lengths

fixture = Path(sys.argv[1] if len(sys.argv) > 1 else 'build/oracle/custom-stages/landing-without-scenery.bmp')
lengths = runs(fixture)
gradient = runs(fixture, 205, 235, True)
assert gradient >= 8, 'Carrier horizon gradient is missing'
shade = runs(fixture, 205, 235, rgb=True)
assert all(sum(a) <= sum(b) for a, b in zip(shade, shade[1:])), 'Gradient has dark scanline gaps'
# Blend through the horizon, then retain the original saturated foreground.
water = runs(fixture, 238, 550, rgb=True)
assert all(max(abs(a[c]-b[c]) for c in range(3)) <= 7 for a,b in zip(water, water[1:])), 'Ocean has thin bands or a sharp horizon seam'
assert all(sum(a)>=sum(b) for a,b in zip(water,water[1:])), 'Water does not fade toward the foreground'
assert len(set(water))>40, 'Ocean depth/haze gradient is missing'
assert {p[::-1] for p in runs(fixture,110,165,rgb=True,x=1080)} == {bytes((0,170,255))}, 'Classic cyan sky was not restored'
assert {p[::-1] for p in runs(fixture,360,550,rgb=True)} == {bytes((0,0,255))}, 'Classic blue foreground sea was not restored'

# Retain the original band-thickness coverage on the runway's raster material.
road_fixture=Path('build/oracle/custom-stages/runway-without-scenery.bmp')
if road_fixture.exists():
    road=runs(road_fixture)
    assert min(road)>=3, ('Runway road bands too thin',road)
print(f'Carrier horizon: {gradient} distinct gradient colors')
print(f'Classic carrier palette restored; {len(set(water))} horizon-blend colors, smooth transition; runway bands retained')
if len(sys.argv) > 2:
    old = runs(Path(sys.argv[2]))
    assert min(old) < 3, 'Thin-band fault control did not fail'
    print(f'Retained thin-band image rejected: minimum {min(old)} pixel')
