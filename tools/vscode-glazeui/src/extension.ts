// Extensionens nav — aktivering, kommandona (lägesväxlingen a/b/c +
// Öppna i GlazeBuilder) och registreringen av custom-editorn.
// Rendering: ALLTID via glaze-render (buntad binär) — extensionen parsar
// aldrig tripletter själv (planens grundregel).

import * as vscode from 'vscode';

import {
    activeGlazeuiUri,
    openInBuilder,
    updateBuilderContext,
} from './builderLocator';
import { GlazePreviewProvider } from './previewProvider';
import { Renderer } from './renderer';
import { RenderScheduler } from './scheduler';
import { SidePreviewManager } from './sidePreview';

export function activate(context: vscode.ExtensionContext): void {
    const log = vscode.window.createOutputChannel('GlazeUI');
    const scheduler = new RenderScheduler();
    const renderer = new Renderer(context, log);
    const sidePreviews = new SidePreviewManager(context, renderer, scheduler);
    const extBase = context.extensionUri.fsPath;   // för att hitta buntad GlazeBuilder

    // Kommandona får resurs-URI:n som argument från editor/title-knapparna;
    // från paletten faller de tillbaka på aktiv flik.
    const uriOrActive = (uri?: vscode.Uri) => uri ?? activeGlazeuiUri();

    context.subscriptions.push(
        log,
        sidePreviews,
        vscode.window.registerCustomEditorProvider(
            GlazePreviewProvider.viewType,
            new GlazePreviewProvider(context, renderer, scheduler),
            {
                webviewOptions: { retainContextWhenHidden: true },
                supportsMultipleEditorsPerDocument: false,
            },
        ),
        vscode.commands.registerCommand('glazeui.showText', (uri?: vscode.Uri) => {
            const u = uriOrActive(uri);
            if (u) void vscode.commands.executeCommand('vscode.openWith', u, 'default');
        }),
        vscode.commands.registerCommand(
            'glazeui.showPreview',
            (uri?: vscode.Uri) => {
                const u = uriOrActive(uri);
                if (u)
                    void vscode.commands.executeCommand(
                        'vscode.openWith',
                        u,
                        GlazePreviewProvider.viewType,
                    );
            },
        ),
        vscode.commands.registerCommand(
            'glazeui.toggleSidePreview',
            (uri?: vscode.Uri) => {
                const u = uriOrActive(uri);
                if (u) sidePreviews.toggle(u);
            },
        ),
        vscode.commands.registerCommand(
            'glazeui.openInBuilder',
            (uri?: vscode.Uri) => {
                const u = uriOrActive(uri);
                if (u) openInBuilder(u, log, extBase);
            },
        ),
        // Knappens tillgänglighet följer aktiv flik + inställningarna.
        vscode.window.tabGroups.onDidChangeTabs(() => updateBuilderContext(extBase)),
        vscode.workspace.onDidChangeConfiguration((e) => {
            if (e.affectsConfiguration('glazeui')) updateBuilderContext(extBase);
        }),
    );
    updateBuilderContext(extBase);
}

export function deactivate(): void {}
