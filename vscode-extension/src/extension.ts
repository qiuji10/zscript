import * as fs from "fs";
import * as path from "path";
import * as vscode from "vscode";
import {
  LanguageClient,
  LanguageClientOptions,
  ServerOptions,
  TransportKind,
} from "vscode-languageclient/node";

let client: LanguageClient | undefined;

function resolveServerPath(): string | undefined {
  const config = vscode.workspace.getConfiguration("zscript");
  const configured: string = config.get("serverPath") ?? "";

  // 1. Explicit setting (non-default)
  if (configured && configured !== "zsc" && fs.existsSync(configured)) {
    return configured;
  }

  // 2. Candidate paths relative to each workspace folder
  const candidates: string[] = [];
  for (const folder of vscode.workspace.workspaceFolders ?? []) {
    const root = folder.uri.fsPath;
    candidates.push(
      path.join(root, "build", "zsc"),
      path.join(root, "build", "Release", "zsc"),
      path.join(root, "build", "Debug", "zsc"),
      path.join(root, "out", "zsc"),
    );
  }

  for (const c of candidates) {
    if (fs.existsSync(c)) return c;
  }

  // 3. Fallback: rely on PATH (will fail with ENOENT if not there)
  return configured || "zsc";
}

export function activate(context: vscode.ExtensionContext) {
  const serverPath = resolveServerPath();
  if (!serverPath) {
    vscode.window.showErrorMessage(
      "ZScript: cannot find zsc binary. Set zscript.serverPath in settings."
    );
    return;
  }

  const serverOptions: ServerOptions = {
    command: serverPath,
    args: ["lsp"],
    transport: TransportKind.stdio,
  };

  const clientOptions: LanguageClientOptions = {
    documentSelector: [{ scheme: "file", language: "zscript" }],
    synchronize: {
      fileEvents: vscode.workspace.createFileSystemWatcher("**/*.zs"),
    },
  };

  client = new LanguageClient(
    "zscript-lsp",
    "ZScript Language Server",
    serverOptions,
    clientOptions
  );

  client.start().catch((err) => {
    vscode.window.showErrorMessage(
      `ZScript LSP failed to start: ${err.message}\n` +
        `Check that zsc is at: ${serverPath}\n` +
        `Or set zscript.serverPath in your settings.`
    );
  });

  context.subscriptions.push(
    vscode.debug.registerDebugConfigurationProvider(
      "zscript",
      new ZScriptDebugConfigProvider(serverPath)
    )
  );

  context.subscriptions.push(
    vscode.debug.registerDebugAdapterDescriptorFactory(
      "zscript",
      new ZScriptDebugAdapterFactory(serverPath)
    )
  );
}

export function deactivate(): Thenable<void> | undefined {
  return client?.stop();
}

// ── DAP support ──────────────────────────────────────────────────────────────

class ZScriptDebugConfigProvider implements vscode.DebugConfigurationProvider {
  constructor(private serverPath: string) {}

  resolveDebugConfiguration(
    _folder: vscode.WorkspaceFolder | undefined,
    config: vscode.DebugConfiguration
  ): vscode.DebugConfiguration {
    if (!config.type && !config.request && !config.name) {
      const editor = vscode.window.activeTextEditor;
      if (editor && editor.document.languageId === "zscript") {
        config.type    = "zscript";
        config.name    = "Run ZScript";
        config.request = "launch";
        config.program = editor.document.fileName;
      }
    }
    if (!config.program) {
      vscode.window.showErrorMessage("No ZScript file specified to debug.");
    }
    return config;
  }
}

class ZScriptDebugAdapterFactory
  implements vscode.DebugAdapterDescriptorFactory
{
  constructor(private serverPath: string) {}

  createDebugAdapterDescriptor(
    _session: vscode.DebugSession
  ): vscode.ProviderResult<vscode.DebugAdapterDescriptor> {
    return new vscode.DebugAdapterExecutable(this.serverPath, ["dap"]);
  }
}
