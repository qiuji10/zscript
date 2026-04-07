import * as fs from "fs";
import * as path from "path";
import * as vscode from "vscode";
import {
  LanguageClient,
  LanguageClientOptions,
  RevealOutputChannelOn,
  ServerOptions,
  TransportKind,
  Trace,
} from "vscode-languageclient/node";

let client: LanguageClient | undefined;
let outputChannel: vscode.OutputChannel | undefined;

function resolveServerPath(): string {
  const config = vscode.workspace.getConfiguration("zscript");
  const configured: string = config.get("serverPath") ?? "";

  // 1. Explicit path in settings
  if (configured && configured !== "zsc" && fs.existsSync(configured)) {
    return configured;
  }

  // 2. Well-known build output paths relative to workspace roots
  const candidates: string[] = [];
  for (const folder of vscode.workspace.workspaceFolders ?? []) {
    const root = folder.uri.fsPath;
    candidates.push(
      path.join(root, "build", "Debug",   "zsc.exe"),
      path.join(root, "build", "Release", "zsc.exe"),
      path.join(root, "build", "Debug",   "zsc"),
      path.join(root, "build", "Release", "zsc"),
      path.join(root, "build", "zsc.exe"),
      path.join(root, "build", "zsc"),
      path.join(root, "out",   "zsc"),
    );
  }

  for (const c of candidates) {
    if (fs.existsSync(c)) return c;
  }

  // 3. Fall back to PATH
  return configured || "zsc";
}

function traceLevel(): Trace {
  const level = vscode.workspace
    .getConfiguration("zscript")
    .get<string>("trace.server", "off");
  switch (level) {
    case "messages": return Trace.Messages;
    case "verbose":  return Trace.Verbose;
    default:         return Trace.Off;
  }
}

async function startClient(context: vscode.ExtensionContext): Promise<void> {
  const serverPath = resolveServerPath();

  outputChannel ??= vscode.window.createOutputChannel("ZScript Language Server");

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
    outputChannel,
    revealOutputChannelOn: RevealOutputChannelOn.Error,
    // Let the server advertise its own capabilities; no client-side overrides needed
    initializationOptions: {},
  };

  client = new LanguageClient(
    "zscript-lsp",
    "ZScript Language Server",
    serverOptions,
    clientOptions
  );

  client.setTrace(traceLevel());

  try {
    await client.start();
  } catch (err: unknown) {
    const msg = err instanceof Error ? err.message : String(err);
    vscode.window.showErrorMessage(
      `ZScript LSP failed to start: ${msg}\n` +
      `Check that 'zsc' is at: ${serverPath}\n` +
      `Or set zscript.serverPath in settings.`
    );
  }
}

export async function activate(context: vscode.ExtensionContext): Promise<void> {
  await startClient(context);

  // Re-apply trace level when the setting changes
  context.subscriptions.push(
    vscode.workspace.onDidChangeConfiguration((e) => {
      if (e.affectsConfiguration("zscript.trace.server") && client) {
        client.setTrace(traceLevel());
      }
    })
  );

  context.subscriptions.push(
    vscode.commands.registerCommand("zscript.restartServer", async () => {
      if (client) {
        await client.stop();
        client = undefined;
      }
      await startClient(context);
      vscode.window.showInformationMessage("ZScript language server restarted.");
    })
  );

  context.subscriptions.push(
    vscode.commands.registerCommand("zscript.showServerPath", () => {
      vscode.window.showInformationMessage(
        `ZScript server path: ${resolveServerPath()}`
      );
    })
  );

  context.subscriptions.push(
    vscode.commands.registerCommand("zscript.showOutput", () => {
      outputChannel?.show(true);
    })
  );

  // DAP support
  context.subscriptions.push(
    vscode.debug.registerDebugConfigurationProvider(
      "zscript",
      new ZScriptDebugConfigProvider()
    )
  );

  context.subscriptions.push(
    vscode.debug.registerDebugAdapterDescriptorFactory(
      "zscript",
      new ZScriptDebugAdapterFactory()
    )
  );
}

export async function deactivate(): Promise<void> {
  if (client) await client.stop();
}

// ── DAP support ──────────────────────────────────────────────────────────────

class ZScriptDebugConfigProvider implements vscode.DebugConfigurationProvider {
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

class ZScriptDebugAdapterFactory implements vscode.DebugAdapterDescriptorFactory {
  createDebugAdapterDescriptor(
    _session: vscode.DebugSession
  ): vscode.ProviderResult<vscode.DebugAdapterDescriptor> {
    return new vscode.DebugAdapterExecutable(resolveServerPath(), ["dap"]);
  }
}
