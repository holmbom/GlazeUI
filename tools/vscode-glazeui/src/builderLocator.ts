// "Öppna i GlazeBuilder". GlazeBuilder BUNTAS numera (generisk binär i
// bin/<target>/, precis som glaze-render) och används som fallback, så knappen
// i praktiken alltid är tillgänglig. Ordningen:
//   1. inställningen glazeui.builderPath (explicit override — "en annan builder")
//   2. konventionen build/apps/GlazeBuilder/… (projektets EGET bygge — dess
//      egna widgets ritas riktigt; uppåt från .glazeui-filen + arbetsytans rötter)
//   3. den buntade generiska binären (okända projekt-widgets → stand-in)
// Knappen styrs av context-nyckeln glazeui.builderAvailable.

import { spawn } from 'node:child_process';
import * as fs from 'node:fs';
import * as os from 'node:os';
import * as path from 'node:path';
import * as vscode from 'vscode';

const KONVENTION =
    process.platform === 'darwin'
        ? 'build/apps/GlazeBuilder/GlazeBuilder.app/Contents/MacOS/GlazeBuilder'
        : process.platform === 'win32'
          ? 'build/apps/GlazeBuilder/GlazeBuilder.exe'
          : 'build/apps/GlazeBuilder/GlazeBuilder';

// Buntad generisk GlazeBuilder i extensionens bin/<plattform-arkitektur>/.
function bundledBuilder(extBase: string): string | undefined {
    const rel =
        process.platform === 'win32'
            ? 'GlazeBuilder.exe'
            : process.platform === 'darwin'
              ? 'GlazeBuilder.app/Contents/MacOS/GlazeBuilder'
              : 'GlazeBuilder';
    const p = path.join(extBase, 'bin', `${process.platform}-${process.arch}`, rel);
    return fs.existsSync(p) ? p : undefined;
}

export function findBuilder(
    docUri: vscode.Uri | undefined,
    extBase: string,
): string | undefined {
    // 1. explicit inställning (finns den inte faller vi vidare — knappen ska
    //    ändå funka via den buntade fallbacken).
    const conf = vscode.workspace
        .getConfiguration('glazeui')
        .get<string>('builderPath', '')
        .trim();
    if (conf) {
        const p = conf.startsWith('~/') ? os.homedir() + conf.slice(1) : conf;
        if (fs.existsSync(p)) return p;
    }
    // 2. projektets eget bygge (konvention) — vinner över den buntade så att
    //    ett projekt med egna widgets får dem riktigt ritade.
    const roots: string[] = [];
    if (docUri?.scheme === 'file') {
        let d = path.dirname(docUri.fsPath);
        for (let i = 0; i < 8; i++) {
            roots.push(d);
            const parent = path.dirname(d);
            if (parent === d) break;
            d = parent;
        }
    }
    for (const f of vscode.workspace.workspaceFolders ?? [])
        if (f.uri.scheme === 'file') roots.push(f.uri.fsPath);
    for (const r of roots) {
        const kandidat = path.join(r, KONVENTION);
        if (fs.existsSync(kandidat)) return kandidat;
    }
    // 3. buntad generisk fallback
    return bundledBuilder(extBase);
}

export function openInBuilder(
    docUri: vscode.Uri,
    log: vscode.OutputChannel,
    extBase: string,
): void {
    const bin = findBuilder(docUri, extBase);
    if (!bin) {
        void vscode.window.showErrorMessage(
            'GlazeBuilder hittades inte — den buntade binären saknas i VSIX:en. ' +
                'Bygg den (build/apps/GlazeBuilder/…) eller ange sökvägen i ' +
                'inställningen "glazeui.builderPath".',
        );
        return;
    }
    log.appendLine(`öppnar i GlazeBuilder: ${bin} ${docUri.fsPath}`);
    // Fristående process — GlazeBuilder lever vidare oberoende av VS Code.
    const child = spawn(bin, [docUri.fsPath], {
        detached: true,
        stdio: 'ignore',
    });
    child.on('error', (e) =>
        void vscode.window.showErrorMessage(
            `Kunde inte starta GlazeBuilder: ${e.message}`,
        ),
    );
    child.unref();
}

// Aktiv .glazeui-flik (text- ELLER custom-editor) — tabGroups täcker båda,
// onDidChangeActiveTextEditor gör det inte.
export function activeGlazeuiUri(): vscode.Uri | undefined {
    const input = vscode.window.tabGroups.activeTabGroup?.activeTab?.input as
        | { uri?: vscode.Uri }
        | undefined;
    const uri = input?.uri;
    return uri && uri.fsPath.endsWith('.glazeui') ? uri : undefined;
}

export function updateBuilderContext(extBase: string): void {
    void vscode.commands.executeCommand(
        'setContext',
        'glazeui.builderAvailable',
        findBuilder(activeGlazeuiUri(), extBase) !== undefined,
    );
}
