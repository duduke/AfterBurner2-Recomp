#!/usr/bin/env python3
"""Check a custom stage pack before running it.

Validates stage.json against assets/custom_stages/stage.schema.json (the
subset of JSON Schema it uses), checks that every asset the stage names exists
inside the pack or its asset folders, and flags things the game accepts but
that will look wrong: lowercase or unsupported characters in on-screen text
(the arcade font is uppercase and has no / or :), PNGs that are not RGBA-ish,
missions without formations to count, and so on.

    python3 validate_stage.py assets/custom_stages/packs/night-raid
    python3 validate_stage.py my-stage/stage.json --assets extra-art/

Exit status 0 = no errors (warnings allowed), 1 = errors. Standard library only.
"""
import argparse
import json
import re
import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[4]
SCHEMA = REPO / 'assets/custom_stages/stage.schema.json'
FONT_OK = re.compile(r"^[A-Z0-9 .,!?'\-+*]*$")
ASSET_KEYS = {'asset', 'hull', 'broken', 'deck', 'island', 'jet', 'water', 'fighter', 'ship', 'music_file'}


class Report:
    def __init__(self):
        self.errors, self.warnings = [], []

    def error(self, where, text):
        self.errors.append(f'{where}: {text}')

    def warn(self, where, text):
        self.warnings.append(f'{where}: {text}')


def check(value, schema, where, r, root):
    """Validate the JSON Schema subset the stage schema uses."""
    if '$ref' in schema:
        ref = schema['$ref']
        node = root
        for part in ref.lstrip('#/').split('/'):
            node = node.get(part, {})
        schema = node
    for key in ('anyOf', 'oneOf'):
        if key in schema:
            trials = []
            for option in schema[key]:
                sub = Report()
                check(value, option, where, sub, root)
                trials.append(sub)
            if not any(not t.errors for t in trials):
                r.error(where, f'matches none of the allowed forms ({trials[0].errors[0] if trials[0].errors else "?"})')
            return
    if 'enum' in schema and value not in schema['enum']:
        r.error(where, f'{value!r} is not one of {schema["enum"]}')
        return
    kind = schema.get('type')
    types = kind if isinstance(kind, list) else [kind] if kind else []
    py = {'object': dict, 'array': list, 'string': str, 'boolean': bool, 'integer': int, 'number': (int, float), 'null': type(None)}
    if types:
        ok = False
        for t in types:
            if t == 'integer' and isinstance(value, bool):
                continue
            if t == 'number' and isinstance(value, bool):
                continue
            if isinstance(value, py.get(t, object)):
                ok = True
        if not ok:
            r.error(where, f'expected {"/".join(types)}, got {type(value).__name__}')
            return
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        if 'minimum' in schema and value < schema['minimum']:
            r.error(where, f'{value} is below {schema["minimum"]}')
        if 'maximum' in schema and value > schema['maximum']:
            r.error(where, f'{value} is above {schema["maximum"]}')
    if isinstance(value, dict):
        props = schema.get('properties', {})
        for req in schema.get('required', []):
            if req not in value:
                r.error(where, f'missing required "{req}"')
        patterns = {re.compile(p): sub for p, sub in schema.get('patternProperties', {}).items()}
        for k, v in value.items():
            matched = [sub for rx, sub in patterns.items() if rx.search(k)]
            if k in props:
                check(v, props[k], f'{where}.{k}', r, root)
            elif matched:
                for sub in matched:
                    check(v, sub, f'{where}.{k}', r, root)
            elif schema.get('additionalProperties') is False:
                r.error(where, f'unknown key "{k}" (typo? see the schema)')
    if isinstance(value, list):
        if 'minItems' in schema and len(value) < schema['minItems']:
            r.error(where, f'needs at least {schema["minItems"]} items')
        if 'maxItems' in schema and len(value) > schema['maxItems']:
            r.error(where, f'allows at most {schema["maxItems"]} items')
        if isinstance(schema.get('items'), dict):
            for i, v in enumerate(value):
                check(v, schema['items'], f'{where}[{i}]', r, root)


def assets_in(node, where=''):
    if isinstance(node, dict):
        for k, v in node.items():
            if k in ASSET_KEYS and isinstance(v, str) and v:
                yield f'{where}.{k}', v
            else:
                yield from assets_in(v, f'{where}.{k}')
    elif isinstance(node, list):
        for i, v in enumerate(node):
            yield from assets_in(v, f'{where}[{i}]')


def png_info(path):
    data = path.read_bytes()[:33]
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        return None
    w, h, depth, color = struct.unpack('>IIBB', data[16:26])
    return w, h, color


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('stage', type=Path, help='pack folder or stage.json')
    ap.add_argument('--assets', type=Path, action='append', default=[], help='extra asset folders, newest first')
    args = ap.parse_args()
    path = args.stage / 'stage.json' if args.stage.is_dir() else args.stage
    home = path.parent
    r = Report()
    try:
        stage = json.loads(path.read_text(encoding='utf-8'))
    except Exception as e:  # noqa: BLE001 - report any parse failure
        print(f'ERROR {path}: cannot read JSON: {e}')
        return 1
    schema = json.loads(SCHEMA.read_text(encoding='utf-8'))
    check(stage, schema, 'stage', r, schema)

    search = list(args.assets) + [home / p for p in stage.get('asset_packs', [])] + [home]
    for where, asset in assets_in(stage, 'stage'):
        if asset.startswith(('/', '\\')) or '..' in Path(asset).parts or ':' in asset:
            r.error(where, f'"{asset}" must be a relative path inside the pack')
            continue
        found = next((d / asset for d in search if (d / asset).is_file()), None)
        if not found:
            r.error(where, f'asset "{asset}" not found in the pack or asset folders')
        elif found.suffix.lower() == '.png':
            info = png_info(found)
            if not info:
                r.error(where, f'"{asset}" is not a PNG')
            elif info[2] not in (2, 6):
                r.warn(where, f'"{asset}" is not RGB/RGBA (color type {info[2]}); transparency may be lost')

    def text(where, value):
        if isinstance(value, str) and value and not FONT_OK.match(value):
            r.warn(where, f'"{value}": the arcade font is uppercase and has no / : or lowercase; it will show garbage')
    text('stage.name', stage.get('name'))
    m = stage.get('mission')
    if m:
        text('stage.mission.briefing', m.get('briefing'))
        text('stage.mission.detail', m.get('detail'))
        if m.get('type', 'destroy') == 'destroy' and not stage.get('formations'):
            r.warn('stage.mission', 'a destroy mission counts formation kills, but there are no formations')
    b = stage.get('boss')
    if b:
        text('stage.boss.name', b.get('name'))
        if not any(p.get('kind') == 'core' for p in b.get('parts', [])):
            r.error('stage.boss.parts', 'needs exactly one part with kind "core"')
    c = stage.get('conditions', {})
    if c.get('sea') and not c.get('art', {}).get('water'):
        r.warn('stage.conditions', 'sea is on but art.water is missing, so no water is drawn')
    if stage.get('formations') and not c.get('art', {}).get('fighter'):
        r.warn('stage.conditions.art', 'formations need art.fighter to be visible')
    if stage.get('carrier_landing') and not stage.get('carrier_art'):
        r.warn('stage', 'carrier_landing without carrier_art keeps the arcade runway look')
    slot = stage.get('slot')
    if slot in (1,):
        r.warn('stage.slot', 'slot 1 starts with the carrier catapult sequence')

    for e in r.errors:
        print('ERROR', e)
    for w in r.warnings:
        print('WARN ', w)
    print(f'{path}: {len(r.errors)} error(s), {len(r.warnings)} warning(s)')
    return 1 if r.errors else 0


if __name__ == '__main__':
    sys.exit(main())
