# Callsign checks: the data

`app/Twist.App/Assets/callcheck.json` is read by
`app/Twist.App/Services/CallCheck.cs` (doubtful callsigns: prefix not
used by radio amateurs, TWIST L locator far from the country of the
callsign). To make it again, for a newer country file:

```sh
mkdir /tmp/cc && cd /tmp/cc && npm init -y
npm install @ham2k/lib-country-files world-atlas@2.0.2
node /path/to/TWIST/tools/callcheck/dump_cty.js > cty.json
pip install shapely
python3 /path/to/TWIST/tools/callcheck/gen_callcheck.py cty.json \
    node_modules/world-atlas/countries-10m.json \
    /path/to/TWIST/app/Twist.App/Assets/callcheck.json
```

Sources: the Amateur Radio Country Files of Jim Reisert AD1C
(country-files.com), packaged by @ham2k/lib-country-files (MIT);
Natural Earth 4.1.0 (public domain), packaged by world-atlas (ISC).
See CREDITS.md.
