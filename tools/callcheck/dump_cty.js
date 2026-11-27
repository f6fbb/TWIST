// node dump_cty.js > cty.json, in a folder where @ham2k/lib-country-files
// is installed (npm install @ham2k/lib-country-files): the AD1C country
// files as gen_callcheck.py reads them.
const req = require('module').createRequire(process.cwd() + '/');
const B = req('@ham2k/lib-country-files').BIGCTY;
process.stdout.write(JSON.stringify({ entities: B.entities, prefix: B.prefix }));
