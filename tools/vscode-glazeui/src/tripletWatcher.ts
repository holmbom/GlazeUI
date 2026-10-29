// Bevakar triplettens render-relevanta filer: <bas>.h + <bas>.glazeui samt
// närmaste assets.bin (samma fyra-nivåers uppåtvandring som glaze-render).
// FSWatcher KRÄVS — GlazeBuilder sparar utanför VS Code; dessutom
// onDidSaveTextDocument för text-läget (planens steg 5). .cpp bevakas inte
// — handlers påverkar aldrig bilden (glaze-render läser dem inte ens).

import * as fs from 'node:fs';
import * as path from 'node:path';
import * as vscode from 'vscode';

export function watchTriplet(
    docUri: vscode.Uri,
    onChange: () => void,
): vscode.Disposable {
    const disposables: vscode.Disposable[] = [];
    const dir = path.dirname(docUri.fsPath);
    const base = path.basename(docUri.fsPath).replace(/\.(glazeui|h|cpp)$/, '');

    // RelativePattern med Uri-bas fungerar även utanför arbetsytans rötter.
    const w = vscode.workspace.createFileSystemWatcher(
        new vscode.RelativePattern(vscode.Uri.file(dir), `${base}.{h,glazeui}`),
    );
    disposables.push(
        w,
        w.onDidChange(onChange),
        w.onDidCreate(onChange),
        w.onDidDelete(onChange),
    );

    // assets.bin: bevaka katalogen där en faktiskt ligger. Finns ingen nu
    // bevakas inget — att en dyker upp mitt i en session är ovanligt, och
    // nästa triplett-spara fångar den ändå.
    let d = dir;
    for (let i = 0; i < 4; i++) {
        if (fs.existsSync(path.join(d, 'assets.bin'))) {
            const aw = vscode.workspace.createFileSystemWatcher(
                new vscode.RelativePattern(vscode.Uri.file(d), 'assets.bin'),
            );
            disposables.push(
                aw,
                aw.onDidChange(onChange),
                aw.onDidCreate(onChange),
                aw.onDidDelete(onChange),
            );
            break;
        }
        const parent = path.dirname(d);
        if (parent === d) break;
        d = parent;
    }

    disposables.push(
        vscode.workspace.onDidSaveTextDocument((saved) => {
            const p = saved.uri.fsPath;
            if (
                p === path.join(dir, `${base}.h`) ||
                p === path.join(dir, `${base}.glazeui`)
            )
                onChange();
        }),
    );

    return vscode.Disposable.from(...disposables);
}
