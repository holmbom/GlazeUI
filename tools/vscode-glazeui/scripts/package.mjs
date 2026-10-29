// Paketerar (eller publicerar, --publish) en per-plattforms-VSIX med rätt
// glaze-render-binär buntad. .vscodeignore utesluter bin/** — scriptet
// appendar temporärt "!bin/<target>/**" så exakt EN binär följer med, och
// återställer filen efteråt.
//
//   node scripts/package.mjs <target> [--publish]
//
// Kräver att bin/<target>/glaze-render(.exe) finns — binären byggs på
// respektive plattform (cmake --build build) och läggs dit med
// scripts/copy-bin.mjs (se README/CI-notisen).

import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROT = path.join(HERE, '..');

const target = process.argv[2];
const publish = process.argv.includes('--publish');
const VALID = ['darwin-arm64', 'darwin-x64', 'win32-x64', 'linux-x64'];
if (!VALID.includes(target)) {
    console.error(`användning: node scripts/package.mjs <${VALID.join('|')}> [--publish]`);
    process.exit(2);
}

// Båda binärerna buntas: glaze-render (rendering) + GlazeBuilder (editorn).
const win = target.startsWith('win32');
const mac = target.startsWith('darwin');
const required = [
    win ? 'glaze-render.exe' : 'glaze-render',
    mac ? 'GlazeBuilder.app' : win ? 'GlazeBuilder.exe' : 'GlazeBuilder',
];
for (const namn of required) {
    const p = path.join(ROT, 'bin', target, namn);
    if (!fs.existsSync(p)) {
        console.error(
            `bin/${target}/${namn} saknas — den här VSIX:en byggs när binärerna finns.\n` +
            `Bygg glaze-render + GlazeBuilder på ${target} (cmake --build build) och lägg dit dem\n` +
            `(node scripts/copy-bin.mjs ${target} på den maskinen).`,
        );
        process.exit(1);
    }
    // Exekverbar bit in i zip:en — vsce bevarar den på Unix-targets.
    // (.app-bundlens inre binär fick sitt läge av copy-bins rekursiva copy.)
    if (!win && namn !== 'GlazeBuilder.app') fs.chmodSync(p, 0o755);
}

const ignorePath = path.join(ROT, '.vscodeignore');
const orig = fs.readFileSync(ignorePath, 'utf8');
fs.writeFileSync(ignorePath, `${orig}\n!bin/${target}/**\n`);

// Kör vsce via dess JS-entry med SAMMA node — spawna ALDRIG .cmd/.bat
// direkt: Node (≥ ~18.20/20.12/24) vägrar det utan shell:true (EINVAL,
// CVE-2024-27980) och felet blev tyst. @vscode/vsce/vsce är cross-platform.
const vsceJs = path.join(ROT, 'node_modules', '@vscode', 'vsce', 'vsce');
const args = [vsceJs, publish ? 'publish' : 'package', '--no-dependencies', '--target', target];
let status = 1;
try {
    const r = spawnSync(process.execPath, args, { cwd: ROT, stdio: 'inherit' });
    if (r.error) console.error(`kunde inte starta vsce: ${r.error.message}`);
    status = r.status ?? 1;
} finally {
    fs.writeFileSync(ignorePath, orig);   // återställ ALLTID
}
process.exit(status);
