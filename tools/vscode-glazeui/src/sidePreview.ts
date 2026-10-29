// Sidopreviewn (läge c — text + live-preview sida-vid-sida): en vanlig
// WebviewPanel i ViewColumn.Beside som delar PreviewSession-logiken med
// custom-editorn. Toggle: finns panelen för dokumentet stängs den, annars
// öppnas en ny bredvid.

import * as path from 'node:path';
import * as vscode from 'vscode';

import { PreviewSession } from './previewSession';
import { Renderer } from './renderer';
import { RenderScheduler } from './scheduler';

export class SidePreviewManager implements vscode.Disposable {
    private panels = new Map<string, vscode.WebviewPanel>();

    constructor(
        private readonly context: vscode.ExtensionContext,
        private readonly renderer: Renderer,
        private readonly scheduler: RenderScheduler,
    ) {}

    toggle(docUri: vscode.Uri): void {
        const key = docUri.fsPath;
        const existing = this.panels.get(key);
        if (existing) {
            existing.dispose();   // onDidDispose städar sessionen + mappen
            return;
        }
        const panel = vscode.window.createWebviewPanel(
            'glazeui.sidePreview',
            `Förhandsvisning: ${path.basename(docUri.fsPath)}`,
            { viewColumn: vscode.ViewColumn.Beside, preserveFocus: true },
            { retainContextWhenHidden: true },
        );
        const session = new PreviewSession(
            this.context,
            this.renderer,
            this.scheduler,
            docUri,
            'side',
            panel.webview,
        );
        this.panels.set(key, panel);
        panel.onDidDispose(() => {
            session.dispose();
            this.panels.delete(key);
        });
    }

    dispose(): void {
        for (const p of [...this.panels.values()]) p.dispose();
        this.panels.clear();
    }
}
