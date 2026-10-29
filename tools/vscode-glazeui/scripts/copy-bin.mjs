// Kopierar färskbyggda glaze-render OCH GlazeBuilder från repots build-katalog
// till bin/<target>/ (exekverbar bit bevaras). Utan argument = värdplattformen.
//
//   node scripts/copy-bin.mjs [darwin-arm64|darwin-x64|win32-x64|linux-x64]
//
// glaze-render är ett CLI (enkel binär, inget bundle ens på Mac); GlazeBuilder
// är en GUI-app — .app-bundle på Mac (kopieras rekursivt), annars en exe/binär.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROT = path.join(HERE, '..');                 // tools/vscode-glazeui
const REPO = path.join(ROT, '..', '..');           // GlazeUI-repot

const target = process.argv[2] ?? `${process.platform}-${process.arch}`;
const VALID = ['darwin-arm64', 'darwin-x64', 'win32-x64', 'linux-x64'];
if (!VALID.includes(target)) {
    console.error(`okänt target: ${target} (giltiga: ${VALID.join(', ')})`);
    process.exit(2);
}

const win = target.startsWith('win32');
const mac = target.startsWith('darwin');

const items = [
    {
        namn: win ? 'glaze-render.exe' : 'glaze-render',
        kalla: path.join(REPO, 'build', 'apps', 'glaze-render',
            win ? 'glaze-render.exe' : 'glaze-render'),
    },
    mac
        ? {
              namn: 'GlazeBuilder.app',
              kalla: path.join(REPO, 'build', 'apps', 'GlazeBuilder', 'GlazeBuilder.app'),
              dir: true,
          }
        : {
              namn: win ? 'GlazeBuilder.exe' : 'GlazeBuilder',
              kalla: path.join(REPO, 'build', 'apps', 'GlazeBuilder',
                  win ? 'GlazeBuilder.exe' : 'GlazeBuilder'),
          },
];

// VSIX:en buntar STATISKA binärer. Ett delat build/ (GLAZE_SHARED) hade gett
// en exe vars libglaze.dll/.so/.dylib aldrig följer med — vägra i stället.
const cache = path.join(REPO, 'build', 'CMakeCache.txt');
if (fs.existsSync(cache) &&
    /^GLAZE_SHARED:BOOL=(ON|TRUE|1|YES|Y)\r?$/mi.test(fs.readFileSync(cache, 'utf8'))) {
    console.error('build/ är byggt med GLAZE_SHARED=ON — VSIX:en buntar statiska '
        + 'binärer. Bygg build/ utan GLAZE_SHARED (cmake -S . -B build '
        + '-DGLAZE_SHARED=OFF) och kör igen.');
    process.exit(1);
}

const destDir = path.join(ROT, 'bin', target);
fs.mkdirSync(destDir, { recursive: true });

for (const it of items) {
    if (!fs.existsSync(it.kalla)) {
        console.error(`${it.kalla} saknas — bygg först: cmake --build build`);
        process.exit(1);
    }
    const mal = path.join(destDir, it.namn);
    if (it.dir) {
        fs.rmSync(mal, { recursive: true, force: true });
        fs.cpSync(it.kalla, mal, { recursive: true });          // bevarar mode
    } else {
        fs.copyFileSync(it.kalla, mal);
        if (!win) fs.chmodSync(mal, 0o755);
    }
    console.log(`${it.kalla} → bin/${target}/${it.namn}`);
}
