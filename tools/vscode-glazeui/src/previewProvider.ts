// Custom-editorn (läge a — bild, default vid öppning). Read-only:
// previewn ÄR bilden; texten redigeras i den vanliga texteditorn
// (vscode.openWith "default" — läge b). Varje editor-flik får en egen
// PreviewSession som städas när fliken stängs.

import * as vscode from 'vscode';

import { PreviewSession } from './previewSession';
import { Renderer } from './renderer';
import { RenderScheduler } from './scheduler';

export class GlazePreviewProvider
    implements vscode.CustomReadonlyEditorProvider
{
    static readonly viewType = 'glazeui.preview';

    constructor(
        private readonly context: vscode.ExtensionContext,
        private readonly renderer: Renderer,
        private readonly scheduler: RenderScheduler,
    ) {}

    openCustomDocument(uri: vscode.Uri): vscode.CustomDocument {
        return { uri, dispose: () => {} };
    }

    resolveCustomEditor(
        document: vscode.CustomDocument,
        webviewPanel: vscode.WebviewPanel,
    ): void {
        const session = new PreviewSession(
            this.context,
            this.renderer,
            this.scheduler,
            document.uri,
            'editor',
            webviewPanel.webview,
        );
        webviewPanel.onDidDispose(() => session.dispose());
    }
}
