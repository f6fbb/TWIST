#!/usr/bin/env python3
"""Makes app/Twist.App/Assets/callcheck.json, the data of the callsign
checks of TWIST Console (Services/CallCheck.cs), from:
  cty.json             the Amateur Radio Country Files of Jim Reisert AD1C
                       (country-files.com), as dumped by dump_cty.js from
                       the npm package @ham2k/lib-country-files
  countries-10m.json   Natural Earth admin-0 countries (public domain), as
                       TopoJSON in the npm package world-atlas 2.0.2
Usage: gen_callcheck.py cty.json countries-10m.json out.json   (needs shapely)

What it holds:
  prefixes  prefix -> entity (the longest prefix of a callsign gives its
            DXCC entity); the exact callsigns of the file are left out
  entities  [prefix, name, lat, lon, country or -1]: the country is the
            Natural Earth country under the entity's point (or the nearest
            one, less than 100 km away), -1 if none (a small island, a reef)
  squares   locator square (JN12) -> the countries within it or less
            than about 150 km away (land squares only)
"""
import json, math, sys
from shapely.geometry import box, Point, Polygon, MultiPolygon
from shapely.strtree import STRtree
from shapely.prepared import prep

cty, topo_path, out = sys.argv[1:4]
d = json.load(open(cty))
topo = json.load(open(topo_path))

# ---- TopoJSON -> shapely ----
sx, sy = topo['transform']['scale']; tx, ty = topo['transform']['translate']
arcs = []
for a in topo['arcs']:
    x = y = 0; pts = []
    for dx, dy in a:
        x += dx; y += dy
        pts.append((x * sx + tx, y * sy + ty))
    arcs.append(pts)
def ring(ids):
    pts = []
    for i in ids:
        a = arcs[i] if i >= 0 else arcs[~i][::-1]
        pts.extend(a if not pts else a[1:])
    return pts
def poly(rings):
    rs = [r for r in (ring(x) for x in rings) if len(r) >= 4]
    return Polygon(rs[0], rs[1:]) if rs else None
def geom(g):
    ps = [g['arcs']] if g['type'] == 'Polygon' else g['arcs'] if g['type'] == 'MultiPolygon' else []
    ps = [x for x in (poly(r) for r in ps) if x is not None]
    return MultiPolygon(ps) if ps else None
names, geoms = [], []
for g in topo['objects']['countries']['geometries']:
    s = geom(g)
    if s is None or s.is_empty: continue
    if not s.is_valid: s = s.buffer(0)
    names.append(g['properties']['name']); geoms.append(s)
tree = STRtree(geoms)
print(len(names), 'countries', file=sys.stderr)

def km_box(lon0, lat0, lon1, lat1, km):
    dlat = km / 111.0
    dlon = km / (111.0 * max(0.2, math.cos(math.radians(max(abs(lat0), abs(lat1))))))
    return box(lon0 - dlon, max(-90, lat0 - dlat), lon1 + dlon, min(90, lat1 + dlat))

# ---- entities ----
ents = sorted(d['entities'].values(), key=lambda e: e['entityPrefix'])
eidx = {}
entities = []
unplaced = []
for e in ents:
    p = Point(e['lon'], e['lat'])
    c = -1
    hits = [i for i in tree.query(p) if geoms[i].contains(p)]
    if hits: c = int(hits[0])
    elif e['name'] == 'Antarctica': c = names.index('Antarctica')
    else:                                   # a point in the sea: the nearest country,
        b = km_box(e['lon'], e['lat'], e['lon'], e['lat'], 100)   # if less than 100 km away
        near = [int(i) for i in tree.query(b) if geoms[i].intersects(b)]
        if near: c = min(near, key=lambda i: geoms[i].distance(p))
    # (else a small island or a reef missing from the map: the entity is
    # then only its point; CallCheck also accepts 500 km around the point)
    if c < 0: unplaced.append(e['name'])
    eidx[e['entityPrefix']] = len(entities)
    entities.append([e['entityPrefix'], e['name'], round(e['lat'], 2), round(e['lon'], 2), c])
print(len(entities), 'entities,', len(unplaced), 'without a country:', ', '.join(unplaced), file=sys.stderr)

prefixes = {p: eidx[m['p']] for p, m in sorted(d['prefix'].items()) if m['p'] in eidx and '/' not in p}

# ---- squares ----
squares = {}
preps = [prep(g) for g in geoms]
for f1 in range(18):
    for f2 in range(18):
        for s1 in range(10):
            for s2 in range(10):
                lon0 = -180 + f1 * 20 + s1 * 2; lat0 = -90 + f2 * 10 + s2
                b = km_box(lon0, lat0, lon0 + 2, lat0 + 1, 150)
                cs = sorted(int(i) for i in tree.query(b) if preps[i].intersects(b))
                if cs:
                    squares[chr(65 + f1) + chr(65 + f2) + str(s1) + str(s2)] = cs
print(len(squares), 'squares', file=sys.stderr)

json.dump({'source': 'AD1C country files (country-files.com) via @ham2k/lib-country-files; Natural Earth 4.1.0 via world-atlas 2.0.2',
           'countries': names, 'entities': entities, 'prefixes': prefixes, 'squares': squares},
          open(out, 'w'), separators=(',', ':'))
