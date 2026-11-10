#!/usr/bin/env node
'use strict';
// Concatenates the header-only part of src/implementation/core into one header (rule 7.1 single_header, 7.2 core/).
// usage: node tools/single_header.js <output path>
// Headers that have a .cpp beside them are left out because a single header cannot carry their definitions.

const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..');
const CORE = path.join(ROOT, 'src', 'implementation', 'core');
const output = process.argv[2];
if (!output) {
  console.error('usage: node tools/single_header.js <output path>');
  process.exit(2);
}

const headers = fs.readdirSync(CORE).filter((name) => name.endsWith('.h'));
const headerOnly = headers.filter((name) => !fs.existsSync(path.join(CORE, name.replace(/\.h$/, '.cpp'))));
const text = new Map(headerOnly.map((name) => [name, fs.readFileSync(path.join(CORE, name), 'utf8').replace(/\r\n/g, '\n')]));

function localIncludes(source) {
  return [...source.matchAll(/^#include\s+"(?:implementation\/core\/)?([^"/]+\.h)"\s*$/gm)].map((m) => m[1]).filter((name) => name !== 'kiotty_spec.h');
}

const ordered = [];
const state = new Map();
function visit(name) {
  if (!text.has(name)) throw new Error(`${name} is not header-only but is included by the single header`);
  if (state.get(name) === 'done') return;
  if (state.get(name) === 'visiting') throw new Error(`include cycle at ${name}`);
  state.set(name, 'visiting');
  for (const dependency of localIncludes(text.get(name))) visit(dependency);
  state.set(name, 'done');
  ordered.push(name);
}
[...text.keys()].sort().forEach(visit);

const systemIncludes = new Set();
const bodies = ordered.map((name) => {
  const lines = text.get(name).split('\n').filter((line) => {
    if (/^#pragma once\s*$/.test(line)) return false;
    if (/^#include\s+"/.test(line)) return false;
    const system = /^#include\s+<([^>]+)>\s*$/.exec(line);
    if (system) { systemIncludes.add(system[1]); return false; }
    return true;
  });
  return lines.join('\n').trim();
});

const result = ['#pragma once', '', '#include "kiotty_spec.h"', '', ...[...systemIncludes].sort().map((name) => `#include <${name}>`), '', ...bodies.map((body) => body + '\n')].join('\n');
fs.mkdirSync(path.dirname(path.resolve(output)), { recursive: true });
fs.writeFileSync(path.resolve(output), result.replace(/\n{3,}/g, '\n\n'));
console.log(`${ordered.length} headers -> ${output}`);
