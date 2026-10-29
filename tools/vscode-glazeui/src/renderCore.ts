// renderCore — vscode-FRI kärna för glaze-render-anropet: spawn + parsning
// av stdout-kontraktet och exit-koderna. Hålls fri från vscode-modulen så
// den kan röktestas med ren node mot den riktiga binären.
//
// glaze-renders CLI-kontrakt (apps/glaze-render/src/main.cpp):
//   glaze-render <fil.glazeui> -o <ut.png>
//   exit 0 = OK, stdout-rad "<path>: <w>x<h> px (scale <s>)"
//   exit 1 = reader-fail-safe (skadat stängsel/saknad fil) — diagnos på stderr
//   exit 2 = render-/IO-fel eller felaktig användning

import { spawn } from 'node:child_process';

export interface RenderOk {
    ok: true;
    width: number;   // fysiska px (retained-RT:n 1:1)
    height: number;
    scale: number;   // uiScale — webviewen visar bilden i width/scale CSS-px
}

export interface RenderFail {
    ok: false;
    exitCode: number | null;   // null = kunde inte starta/avbröts
    message: string;           // stderr-diagnosen (visas i felvyn)
}

export type RenderResult = RenderOk | RenderFail;

// Kontraktsraden: printf("%s: %dx%d px (scale %g)\n", ...). Sista matchande
// raden gäller — stdout ska vara ren men parsern är tålig mot extra rader.
const KONTRAKTSRAD = /^(.*): (\d+)x(\d+) px \(scale ([0-9.]+)\)$/;

export function parseContractLine(
    stdout: string,
): { pngPath: string; width: number; height: number; scale: number } | undefined {
    let hit;
    for (const line of stdout.split(/\r?\n/)) {
        const m = KONTRAKTSRAD.exec(line.trim());
        if (m) {
            hit = {
                pngPath: m[1],
                width: Number(m[2]),
                height: Number(m[3]),
                scale: Number(m[4]),
            };
        }
    }
    if (hit && (!(hit.scale > 0) || hit.width <= 0 || hit.height <= 0))
        return undefined;
    return hit;
}

export function renderOnce(
    binPath: string,
    inPath: string,
    outPath: string,
    timeoutMs = 20000,
): Promise<RenderResult> {
    return new Promise((resolve) => {
        let done = false;
        const finish = (r: RenderResult) => {
            if (!done) {
                done = true;
                resolve(r);
            }
        };

        let child;
        try {
            child = spawn(binPath, [inPath, '-o', outPath], {
                stdio: ['ignore', 'pipe', 'pipe'],
            });
        } catch (e) {
            finish({
                ok: false,
                exitCode: null,
                message: `kunde inte starta ${binPath}: ${(e as Error).message}`,
            });
            return;
        }

        let stdout = '';
        let stderr = '';
        child.stdout.on('data', (d) => (stdout += d));
        child.stderr.on('data', (d) => (stderr += d));

        // Säkerhetsnät — glaze-render renderar EN frame och avslutar; hänger
        // den (t.ex. utan WindowServer-session) får vyn ett fel i stället
        // för en evig spinner.
        const timer = setTimeout(() => child.kill('SIGKILL'), timeoutMs);

        child.on('error', (err) => {
            clearTimeout(timer);
            finish({
                ok: false,
                exitCode: null,
                message: `kunde inte starta ${binPath}: ${err.message}`,
            });
        });

        child.on('close', (code, signal) => {
            clearTimeout(timer);
            if (signal) {
                finish({
                    ok: false,
                    exitCode: null,
                    message: `glaze-render avbröts (${signal}) — tog mer än ${timeoutMs / 1000}s?`,
                });
                return;
            }
            if (code === 0) {
                const p = parseContractLine(stdout);
                if (!p) {
                    finish({
                        ok: false,
                        exitCode: 0,
                        message: `kunde inte tolka glaze-renders utdata:\n${stdout}`,
                    });
                    return;
                }
                finish({ ok: true, width: p.width, height: p.height, scale: p.scale });
                return;
            }
            finish({
                ok: false,
                exitCode: code,
                message: stderr.trim() || `glaze-render avslutades med kod ${code}`,
            });
        });
    });
}
