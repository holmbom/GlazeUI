// PreviewSession — en webview som visar EN .glazeui-fil. Delas av
// custom-editorn (läge a) och sidopreviewn (läge c): samma renderer, samma
// webview-HTML, samma filbevakning. Postar 'bild' (med CSS-bredd =
// fysiska px / scale — HiDPI-beslutet, planen §6) eller 'fel'
// (stderr-diagnosen — förstklassig felvy med Öppna-som-text-knapp).

import * as crypto from 'node:crypto';
import * as fs from 'node:fs';
import * as vscode from 'vscode';

import { Renderer } from './renderer';
import { RenderScheduler } from './scheduler';
import { watchTriplet } from './tripletWatcher';

export class PreviewSession implements vscode.Disposable {
    private disposables: vscode.Disposable[] = [];
    private version = 0;   // cache-buster — webviews cachar bild-URI:er
    private disposed = false;

    constructor(
        private readonly context: vscode.ExtensionContext,
        private readonly renderer: Renderer,
        private readonly scheduler: RenderScheduler,
        readonly docUri: vscode.Uri,
        // 'editor' | 'side' — skiljer utfil och scheduler-nyckel så två
        // vyer av samma dokument aldrig delar PNG eller släcker varandra.
        private readonly kind: string,
        private readonly webview: vscode.Webview,
    ) {
        webview.options = {
            enableScripts: true,
            localResourceRoots: [
                renderer.previewRoot,
                vscode.Uri.joinPath(context.extensionUri, 'media'),
            ],
        };
        webview.html = this.buildHtml();
        this.disposables.push(
            webview.onDidReceiveMessage((msg) => this.onMessage(msg)),
            watchTriplet(docUri, () => this.requestRender()),
        );
        this.requestRender(0);
    }

    private get schedulerKey(): string {
        return `${this.kind}:${this.docUri.fsPath}`;
    }

    requestRender(delayMs = 150): void {
        this.scheduler.schedule(this.schedulerKey, () => this.doRender(), delayMs);
    }

    private async doRender(): Promise<void> {
        if (this.disposed) return;
        const res = await this.renderer.render(this.docUri, this.kind);
        if (this.disposed) return;
        if (res.ok) {
            this.version++;
            const png = this.renderer.pngPathFor(this.docUri, this.kind);
            const src = `${this.webview.asWebviewUri(png)}?v=${this.version}`;
            void this.webview.postMessage({
                type: 'bild',
                src,
                cssWidth: res.width / res.scale,
                width: res.width,
                height: res.height,
                scale: res.scale,
            });
        } else {
            void this.webview.postMessage({
                type: 'fel',
                diagnos: res.message,
                exitCode: res.exitCode,
            });
        }
    }

    private onMessage(msg: { type?: string } | undefined): void {
        switch (msg?.type) {
            case 'oppnaText':
                void vscode.commands.executeCommand(
                    'vscode.openWith',
                    this.docUri,
                    'default',
                );
                break;
            case 'igen':
                this.requestRender(0);
                break;
        }
    }

    private buildHtml(): string {
        const mediaDir = vscode.Uri.joinPath(this.context.extensionUri, 'media');
        const cssUri = this.webview
            .asWebviewUri(vscode.Uri.joinPath(mediaDir, 'preview.css'))
            .toString();
        const nonce = crypto.randomBytes(16).toString('base64');
        return fs
            .readFileSync(vscode.Uri.joinPath(mediaDir, 'preview.html').fsPath, 'utf8')
            .replaceAll('%CSP_SOURCE%', this.webview.cspSource)
            .replaceAll('%NONCE%', nonce)
            .replaceAll('%CSS_URI%', cssUri);
    }

    dispose(): void {
        this.disposed = true;
        this.scheduler.dispose(this.schedulerKey);
        vscode.Disposable.from(...this.disposables).dispose();
        this.disposables = [];
    }
}
