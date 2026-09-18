import * as vscode from 'vscode';
import { spawn, ChildProcess } from 'child_process';
import * as path from 'path';
import * as fs from 'fs';

let outputChannel: vscode.OutputChannel;
let liveReloadStatusBar: vscode.StatusBarItem;
let isLiveReloadEnabled = false;
let fileWatcher: vscode.FileSystemWatcher | null = null;

export function activate(context: vscode.ExtensionContext) {
    outputChannel = vscode.window.createOutputChannel('Jaguar');
    
    // Create status bar item for live reload
    liveReloadStatusBar = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Right, 100);
    liveReloadStatusBar.command = 'jaguar.liveReload';
    context.subscriptions.push(liveReloadStatusBar);
    updateLiveReloadStatus();

    // Register commands
    const runCommand = vscode.commands.registerCommand('jaguar.run', runFile);
    const checkCommand = vscode.commands.registerCommand('jaguar.check', checkFile);
    const formatCommand = vscode.commands.registerCommand('jaguar.format', formatDocument);
    const liveReloadCommand = vscode.commands.registerCommand('jaguar.liveReload', toggleLiveReload);
    const buildCommand = vscode.commands.registerCommand('jaguar.build', buildProject);

    context.subscriptions.push(runCommand, checkCommand, formatCommand, liveReloadCommand, buildCommand);

    // Register document formatter
    const formatter = vscode.languages.registerDocumentFormattingEditProvider('jaguar', {
        provideDocumentFormattingEdits(document: vscode.TextDocument): vscode.TextEdit[] {
            return formatDocumentEdits(document);
        }
    });
    context.subscriptions.push(formatter);

    // Register diagnostic collection (for linting errors)
    const diagnosticCollection = vscode.languages.createDiagnosticCollection('jaguar');
    context.subscriptions.push(diagnosticCollection);

    // Listen for file saves to run linter
    const onSaveListener = vscode.workspace.onDidSaveTextDocument((document) => {
        if (document.languageId === 'jaguar') {
            const config = vscode.workspace.getConfiguration('jaguar');
            if (config.get<boolean>('lintOnSave')) {
                runLinter(document, diagnosticCollection);
            }
            if (config.get<boolean>('formatOnSave')) {
                vscode.commands.executeCommand('jaguar.format');
            }
            if (isLiveReloadEnabled && config.get<boolean>('enableLiveReload')) {
                runFile();
            }
        }
    });
    context.subscriptions.push(onSaveListener);

    // Listen for opening Jaguar files
    const onOpenListener = vscode.workspace.onDidOpenTextDocument((document) => {
        if (document.languageId === 'jaguar') {
            const config = vscode.workspace.getConfiguration('jaguar');
            if (config.get<boolean>('lintOnSave')) {
                runLinter(document, diagnosticCollection);
            }
        }
    });
    context.subscriptions.push(onOpenListener);

    vscode.window.showInformationMessage('Jaguar Language Support is now active! 🐆');
}

function getJagPath(): string {
    const config = vscode.workspace.getConfiguration('jaguar');
    return config.get<string>('executablePath') || 'jag';
}

function runFile() {
    const editor = vscode.window.activeTextEditor;
    if (!editor) {
        vscode.window.showWarningMessage('No active editor');
        return;
    }

    const document = editor.document;
    if (document.languageId !== 'jaguar') {
        vscode.window.showWarningMessage('Current file is not a Jaguar file');
        return;
    }

    const filePath = document.uri.fsPath;
    const jagPath = getJagPath();
    const config = vscode.workspace.getConfiguration('jaguar');
    const showOutput = config.get<boolean>('showOutputOnRun');

    outputChannel.appendLine(`\n▶ Running: ${filePath}`);
    outputChannel.appendLine(`─`.repeat(50));

    const args = ['run', filePath];
    const process = spawn(jagPath, args, {
        cwd: path.dirname(filePath),
        shell: true
    });

    if (showOutput) {
        outputChannel.show(true);
    }

    process.stdout.on('data', (data) => {
        outputChannel.append(data.toString());
    });

    process.stderr.on('data', (data) => {
        outputChannel.append(data.toString());
    });

    process.on('close', (code) => {
        if (code === 0) {
            outputChannel.appendLine(`\n✓ Execution completed successfully`);
        } else {
            outputChannel.appendLine(`\n✗ Execution failed with code ${code}`);
            vscode.window.showErrorMessage(`Jaguar execution failed (exit code: ${code})`);
        }
    });

    process.on('error', (err) => {
        outputChannel.appendLine(`\n✗ Error: ${err.message}`);
        vscode.window.showErrorMessage(
            `Failed to run Jaguar. Make sure '${jagPath}' is installed and in your PATH.`
        );
    });
}

function checkFile() {
    const editor = vscode.window.activeTextEditor;
    if (!editor) {
        vscode.window.showWarningMessage('No active editor');
        return;
    }

    const document = editor.document;
    if (document.languageId !== 'jaguar') {
        vscode.window.showWarningMessage('Current file is not a Jaguar file');
        return;
    }

    const diagnosticCollection = vscode.languages.createDiagnosticCollection('jaguar');
    runLinter(document, diagnosticCollection);
}

function runLinter(document: vscode.TextDocument, diagnosticCollection: vscode.DiagnosticCollection) {
    const filePath = document.uri.fsPath;
    const jagPath = getJagPath();

    outputChannel.appendLine(`\n🔍 Checking: ${filePath}`);

    const args = ['check', filePath];
    const process = spawn(jagPath, args, {
        cwd: path.dirname(filePath),
        shell: true
    });

    let stderr = '';

    process.stderr.on('data', (data) => {
        stderr += data.toString();
    });

    process.stdout.on('data', (data) => {
        outputChannel.append(data.toString());
    });

    process.on('close', (code) => {
        const diagnostics: vscode.Diagnostic[] = [];
        
        if (stderr) {
            outputChannel.appendLine(stderr);
            
            // Parse error messages: filename:line: message
            const lines = stderr.split('\n');
            lines.forEach(line => {
                const match = line.match(/^([^:]+):(\d+):\s*(.*)$/);
                if (match) {
                    const [, file, lineStr, message] = match;
                    const lineNumber = parseInt(lineStr, 10) - 1;
                    
                    if (!isNaN(lineNumber)) {
                        const range = new vscode.Range(lineNumber, 0, lineNumber, 1000);
                        const severity = vscode.DiagnosticSeverity.Error;
                        const diagnostic = new vscode.Diagnostic(range, message, severity);
                        diagnostic.source = 'jaguar';
                        diagnostics.push(diagnostic);
                    }
                }
            });
        }

        diagnosticCollection.set(document.uri, diagnostics);

        if (code === 0 && diagnostics.length === 0) {
            outputChannel.appendLine('✓ No errors found');
            vscode.window.showInformationMessage('Jaguar check passed! ✓');
        } else if (diagnostics.length > 0) {
            vscode.window.showWarningMessage(`Found ${diagnostics.length} issue(s)`);
        }
    });

    process.on('error', (err) => {
        outputChannel.appendLine(`✗ Error: ${err.message}`);
        vscode.window.showErrorMessage(
            `Failed to check Jaguar file. Make sure '${jagPath}' is installed.`
        );
    });
}

function formatDocument() {
    const editor = vscode.window.activeTextEditor;
    if (!editor) {
        vscode.window.showWarningMessage('No active editor');
        return;
    }

    const document = editor.document;
    if (document.languageId !== 'jaguar') {
        vscode.window.showWarningMessage('Current file is not a Jaguar file');
        return;
    }

    const edits = formatDocumentEdits(document);
    const workspaceEdit = new vscode.WorkspaceEdit();
    
    edits.forEach(edit => {
        workspaceEdit.replace(document.uri, edit.range, edit.newText);
    });

    vscode.workspace.applyEdit(workspaceEdit).then(success => {
        if (success) {
            vscode.window.showInformationMessage('Document formatted ✓');
        }
    });
}

function formatDocumentEdits(document: vscode.TextDocument): vscode.TextEdit[] {
    const text = document.getText();
    const lines = text.split('\n');
    const formattedLines: string[] = [];

    // Simple formatter: normalize whitespace and indentation
    let indentLevel = 0;
    const indentSize = 4;

    lines.forEach(line => {
        const trimmed = line.trim();
        
        // Skip empty lines at the end
        if (trimmed === '' && formattedLines.length > 0) {
            formattedLines.push('');
            return;
        }

        // Decrease indent for closing braces
        if (trimmed.startsWith('}') || trimmed.startsWith(']') || trimmed.startsWith(')')) {
            indentLevel = Math.max(0, indentLevel - 1);
        }

        // Add indented line
        const indent = ' '.repeat(indentLevel * indentSize);
        formattedLines.push(indent + trimmed);

        // Increase indent for opening braces/keywords
        if (trimmed.endsWith('{') || trimmed.endsWith('(') || trimmed.endsWith('[') ||
            /^(fun|class|if|elif|else|loop|while|for|try|catch)\b/.test(trimmed)) {
            indentLevel++;
        }
    });

    // Remove trailing empty lines
    while (formattedLines.length > 0 && formattedLines[formattedLines.length - 1] === '') {
        formattedLines.pop();
    }

    const formattedText = formattedLines.join('\n') + '\n';
    
    if (formattedText === text) {
        return [];
    }

    const fullRange = new vscode.Range(
        document.positionAt(0),
        document.positionAt(text.length)
    );

    return [new vscode.TextEdit(fullRange, formattedText)];
}

function toggleLiveReload() {
    isLiveReloadEnabled = !isLiveReloadEnabled;
    updateLiveReloadStatus();

    const editor = vscode.window.activeTextEditor;
    if (editor && editor.document.languageId === 'jaguar') {
        if (isLiveReloadEnabled) {
            startWatchingFile(editor.document);
            vscode.window.showInformationMessage('Live Reload enabled 🔄');
        } else {
            stopWatchingFile();
            vscode.window.showInformationMessage('Live Reload disabled');
        }
    } else {
        vscode.window.showWarningMessage('Please open a Jaguar file first');
    }
}

function updateLiveReloadStatus() {
    if (isLiveReloadEnabled) {
        liveReloadStatusBar.text = '$(sync~spin) Live Reload';
        liveReloadStatusBar.tooltip = 'Live Reload is enabled - click to disable';
        liveReloadStatusBar.color = new vscode.ThemeColor('statusBar.warningForeground');
    } else {
        liveReloadStatusBar.text = '$(sync) Live Reload';
        liveReloadStatusBar.tooltip = 'Live Reload is disabled - click to enable';
        liveReloadStatusBar.color = undefined;
    }
    liveReloadStatusBar.show();
}

function startWatchingFile(document: vscode.TextDocument) {
    stopWatchingFile();

    const pattern = new vscode.RelativePattern(document.uri, '*');
    fileWatcher = vscode.workspace.createFileSystemWatcher(pattern, false, false, false);

    fileWatcher.onDidChange((uri) => {
        if (uri.fsPath === document.uri.fsPath && isLiveReloadEnabled) {
            outputChannel.appendLine(`\n🔄 File changed - reloading...`);
            runFile();
        }
    });
}

function stopWatchingFile() {
    if (fileWatcher) {
        fileWatcher.dispose();
        fileWatcher = null;
    }
}

function buildProject() {
    const editor = vscode.window.activeTextEditor;
    if (!editor) {
        vscode.window.showWarningMessage('No active editor');
        return;
    }

    const document = editor.document;
    const filePath = document.uri.fsPath;
    const jagPath = getJagPath();

    outputChannel.appendLine(`\n🔨 Building: ${filePath}`);

    const args = ['build', filePath];
    const process = spawn(jagPath, args, {
        cwd: path.dirname(filePath),
        shell: true
    });

    outputChannel.show(true);

    process.stdout.on('data', (data) => {
        outputChannel.append(data.toString());
    });

    process.stderr.on('data', (data) => {
        outputChannel.append(data.toString());
    });

    process.on('close', (code) => {
        if (code === 0) {
            outputChannel.appendLine(`\n✓ Build completed successfully`);
            vscode.window.showInformationMessage('Build successful! ✓');
        } else {
            outputChannel.appendLine(`\n✗ Build failed with code ${code}`);
            vscode.window.showErrorMessage(`Build failed (exit code: ${code})`);
        }
    });

    process.on('error', (err) => {
        outputChannel.appendLine(`\n✗ Error: ${err.message}`);
        vscode.window.showErrorMessage(
            `Failed to build. Note: 'jag build' may not be implemented yet.`
        );
    });
}

export function deactivate() {
    stopWatchingFile();
    if (outputChannel) {
        outputChannel.dispose();
    }
}
