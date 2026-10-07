#!/usr/bin/env python3
"""Report runtime object templates and their production entry routes.

Reads the user's ROMs; no template bytes or handler tables are embedded.
Route coverage is an entry-routing inventory, not proof of complete callees,
formation/spawn behavior or independently scheduled gameplay.
"""
from collections import defaultdict
from pathlib import Path
import re
import sys
sys.dont_write_bytecode = True
from build_rom_image import IMAGES, ROMS_DIR


def main():
    even, odd = ((ROMS_DIR / name).read_bytes() for name in IMAGES["maincpu"])
    if len(even) != len(odd):
        raise ValueError("unequal main ROM lengths")
    rom = bytearray(len(even) * 2)
    rom[::2], rom[1::2] = even, odd

    def number(address, size):
        if address < 0 or address + size > len(rom):
            raise ValueError("object table points outside main ROM")
        return int.from_bytes(rom[address:address + size], "big")

    source = (Path(__file__).resolve().parent.parent / "src/main/engine/omainhost.cpp").read_text()
    source = source.split("bool OMainHost::execute", 1)[1].split("else return false", 1)[0]
    branches = list(re.finditer(r"(?:^|\n)\s*(?:else )?if\((entry==[^)]*|contains\(entry,\{.*?\}\))\)", source, re.S))
    routes = {}
    for i, branch in enumerate(branches):
        body = source[branch.end():branches[i + 1].start() if i + 1 < len(branches) else len(source)]
        destination = re.search(r"(OMain\w+)::(\w+)\(", body)
        label = "::".join(destination.groups()) if destination else "publication wait"
        for address in re.findall(r"0x[0-9a-f]+", branch.group(1)):
            entry = int(address, 16)
            if entry in routes:
                raise ValueError(f"duplicate production entry {entry:06X}")
            routes[entry] = label
    if not routes or 0x9810 not in routes or 0x16892 not in routes:
        raise ValueError("production router syntax changed; update the inventory parser")

    handlers = defaultdict(list)
    count = number(0x1c176, 2)
    for slot in range(count):
        pointer = number(0x1c178 + 4 * slot, 4)
        number(pointer, 64)  # Validate the entire template extent.
        handler = number(pointer + 16, 4)
        flags = number(pointer, 1)
        handlers[handler].append((slot, bool(flags & 0x80)))

    routed = empty = unported = 0
    print(f"Runtime object table $01C176: {count} records, {len(handlers)} distinct handlers")
    print("Handler  Route / boundary                         Slots (initial active marked *)")
    for handler, slots in sorted(handlers.items()):
        if not handler:
            label = "empty handler"; empty += len(slots)
        elif handler in routes:
            label = routes[handler]; routed += len(slots)
        else:
            label = "UNPORTED caller / handler"; unported += len(slots)
        positions = ",".join(str(slot) + ("*" if active else "") for slot, active in slots)
        print(f"${handler:06X}  {label:40} {positions}")
    print(f"Templates: {routed} routed, {empty} empty, {unported} unported")
    print("Dynamic handler replacements and formation/spawn callers require their own captures.")


if __name__ == "__main__":
    main()
