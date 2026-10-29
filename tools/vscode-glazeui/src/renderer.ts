// Renderer — vscode-limmet runt renderCore: hittar binären (buntad
// bin/<plattform-arkitektur>/glaze-render, eller inställningen
// glazeui.renderPath), ger varje session en egen PNG under globalStorage
// (ALDRIG default-utfilen bredvid källan — skräpa aldrig i arbetsytan)
// och loggar utfallet i output-kanalen.

import * as crypto from 'node:crypto';
import * as fs from 'node:fs';
import * as os from 'node:os';
import * as vscode from 'vscode';

import { renderOnce, RenderResult } from './renderCore';

export class Renderer {
    private readonly previewDir: vscode.Uri;

    constructor(
        private readonly context: vscode.ExtensionContext,
        private readonly log: vscode.OutputChannel,
    ) {
        this.previewDir = vscode.Uri.joinPath(context.globalStorageUri, 'previews');
    }

    // Webview-CSP:ns localResourceRoots måste innehålla den här katalogen.
    get previewRoot(): vscode.Uri {
        return this.previewDir;
    }

    binaryPath(): string {
        const conf = vscode.workspace
            .getConfiguration('glazeui')
            .get<string>('renderPath', '')
            .trim();
        if (conf) return conf.startsWith('~/') ? os.homedir() + conf.slice(1) : conf;
        const target = `${process.platform}-${process.arch}`;
        const exe = process.platform === 'win32' ? 'glaze-render.exe' : 'glaze-render';
        return vscode.Uri.joinPath(this.context.extensionUri, 'bin', target, exe).fsPath;
    }

    // En PNG per (dokument, session) — editor-vyn och sidopreviewn för samma
    // fil får OLIKA utfiler så två samtidiga renderingar aldrig skriver
    // samma fil.
    pngPathFor(doc: vscode.Uri, suffix: string): vscode.Uri {
        const hash = crypto
            .createHash('sha1')
            .update(doc.fsPath)
            .digest('hex')
            .slice(0, 16);
        return vscode.Uri.joinPath(this.previewDir, `${hash}-${suffix}.png`);
    }

    async render(doc: vscode.Uri, suffix: string): Promise<RenderResult> {
        const bin = this.binaryPath();
        if (!fs.existsSync(bin)) {
            return {
                ok: false,
                exitCode: null,
                message:
                    `render-binären saknas: ${bin}\n\n` +
                    `Den buntas per plattform i VSIX:en (se README). Egen binär ` +
                    `kan pekas ut med inställningen "glazeui.renderPath".`,
            };
        }
        // Zip-vägar kan tappa exekverbar bit — reparera defensivt.
        if (process.platform !== 'win32') {
            try {
                fs.accessSync(bin, fs.constants.X_OK);
            } catch {
                try {
                    fs.chmodSync(bin, 0o755);
                } catch {
                    /* felet syns i spawn-diagnosen i stället */
                }
            }
        }
        await vscode.workspace.fs.createDirectory(this.previewDir);
        const out = this.pngPathFor(doc, suffix);
        const res = await renderOnce(bin, doc.fsPath, out.fsPath);
        if (res.ok)
            this.log.appendLine(
                `render OK: ${doc.fsPath} → ${res.width}x${res.height} px (scale ${res.scale})`,
            );
        else
            this.log.appendLine(
                `render FEL (exit ${res.exitCode}): ${doc.fsPath}\n${res.message}`,
            );
        return res;
    }
}
