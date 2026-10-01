import { Server } from "@modelcontextprotocol/sdk/server/index.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import {
  CallToolRequestSchema,
  ListToolsRequestSchema,
} from "@modelcontextprotocol/sdk/types.js";
import fs from "fs";
import path from "path";
import { spawn, exec } from "child_process";
import { promisify } from "util";
import ini from "ini";

const execAsync = promisify(exec);

const GAME_DIR = "D:\\SteamLibrary\\steamapps\\common\\ELDEN RING\\Game";
const MODENGINE_PROFILE = "C:\\Users\\bigra\\AppData\\Local\\garyttierney\\me3\\config\\profiles\\eldenring-default.me3";
const MODENGINE_BIN = "C:\\Users\\bigra\\AppData\\Local\\Programs\\garyttierney\\me3\\bin\\me3.exe";
const LOG_FILE = path.join(GAME_DIR, "OptiScaler.log");
const INI_FILE = path.join(GAME_DIR, "OptiScaler.ini");

// Global state for tailing
let lastTailPosition = 0;

const server = new Server(
  {
    name: "mcp-debug-server",
    version: "1.0.0",
  },
  {
    capabilities: {
      tools: {},
    },
  }
);

server.setRequestHandler(ListToolsRequestSchema, async () => {
  return {
    tools: [
      {
        name: "read_optiscaler_log",
        description: "Read the OptiScaler.log file from the game directory.",
        inputSchema: {
          type: "object",
          properties: {
            lines: {
              type: "number",
              description: "Number of lines from tail",
              default: 100,
            },
            filter: {
              type: "string",
              description: "Optional regex filter",
            },
          },
        },
      },
      {
        name: "get_ordo_taa_status",
        description: "Parse the OptiScaler.log for all [ORDO] prefixed lines and return a structured summary of TAA detection status.",
        inputSchema: {
          type: "object",
          properties: {},
        },
      },
      {
        name: "edit_ordo_config",
        description: "Read or modify settings in the [OrdoTAA] section of OptiScaler.ini.",
        inputSchema: {
          type: "object",
          properties: {
            action: {
              type: "string",
              enum: ["get", "set"],
            },
            key: {
              type: "string",
            },
            value: {
              type: "string",
            },
          },
          required: ["action", "key"],
        },
      },
      {
        name: "deploy_optiscaler",
        description: "Copy the built dxgi.dll from the build output to the game directory.",
        inputSchema: {
          type: "object",
          properties: {
            build_dir: {
              type: "string",
              description: "Path to build output",
              default: "build/Release",
            },
          },
        },
      },
      {
        name: "launch_game",
        description: "Launch Elden Ring via ModEngine 3 with the correct profile.",
        inputSchema: {
          type: "object",
          properties: {},
        },
      },
      {
        name: "check_game_status",
        description: "Check if eldenring.exe is running, get its PID, memory usage, and whether it's responding.",
        inputSchema: {
          type: "object",
          properties: {},
        },
      },
      {
        name: "tail_log",
        description: "Start tailing OptiScaler.log and return new lines since last call.",
        inputSchema: {
          type: "object",
          properties: {},
        },
      },
      {
        name: "get_crash_info",
        description: "Read Windows Application Event Log for recent Elden Ring crashes.",
        inputSchema: {
          type: "object",
          properties: {},
        },
      },
    ],
  };
});

server.setRequestHandler(CallToolRequestSchema, async (request) => {
  const { name, arguments: args } = request.params;

  try {
    if (name === "read_optiscaler_log") {
      if (!fs.existsSync(LOG_FILE)) {
        return { content: [{ type: "text", text: "OptiScaler.log not found." }] };
      }
      const lines = args?.lines || 100;
      const filter = args?.filter;
      const content = fs.readFileSync(LOG_FILE, "utf-8");
      let logLines = content.split(/\r?\n/);
      
      if (filter) {
        const regex = new RegExp(filter);
        logLines = logLines.filter(line => regex.test(line));
      }
      
      const tailLines = logLines.slice(-lines).join("\n");
      return { content: [{ type: "text", text: tailLines || "No lines matched." }] };
    }

    if (name === "get_ordo_taa_status") {
      if (!fs.existsSync(LOG_FILE)) {
        return { content: [{ type: "text", text: "OptiScaler.log not found." }] };
      }
      const content = fs.readFileSync(LOG_FILE, "utf-8");
      const ordoLines = content.split(/\r?\n/).filter(line => line.includes("[ORDO]"));
      
      return { content: [{ type: "text", text: JSON.stringify({ lines: ordoLines }, null, 2) }] };
    }

    if (name === "edit_ordo_config") {
      const action = args.action;
      const key = args.key;
      let config = {};
      
      if (fs.existsSync(INI_FILE)) {
        config = ini.parse(fs.readFileSync(INI_FILE, "utf-8"));
      }
      
      if (!config.OrdoTAA) {
        config.OrdoTAA = {};
      }

      if (action === "get") {
        return { content: [{ type: "text", text: String(config.OrdoTAA[key] ?? "Not set") }] };
      } else if (action === "set") {
        config.OrdoTAA[key] = args.value;
        fs.writeFileSync(INI_FILE, ini.stringify(config));
        return { content: [{ type: "text", text: `Set ${key} = ${args.value}` }] };
      }
    }

    if (name === "deploy_optiscaler") {
      const buildDir = args?.build_dir || "build/Release";
      const sourceFile = path.resolve(process.cwd(), "..", "..", buildDir, "dxgi.dll");
      const targetFile = path.join(GAME_DIR, "dxgi.dll");
      
      if (!fs.existsSync(sourceFile)) {
        return { content: [{ type: "text", text: `Build file not found at ${sourceFile}` }] };
      }
      
      fs.copyFileSync(sourceFile, targetFile);
      return { content: [{ type: "text", text: `Successfully deployed dxgi.dll to ${targetFile}` }] };
    }

    if (name === "launch_game") {
      const child = spawn(MODENGINE_BIN, ["launch", "-p", MODENGINE_PROFILE], {
        detached: true,
        stdio: "ignore",
      });
      child.unref();
      return { content: [{ type: "text", text: `Launched ModEngine 3. PID: ${child.pid}` }] };
    }

    if (name === "check_game_status") {
      try {
        const { stdout } = await execAsync("tasklist /FI \"IMAGENAME eq eldenring.exe\" /FO CSV /NH");
        if (stdout.includes("eldenring.exe")) {
          return { content: [{ type: "text", text: stdout.trim() }] };
        } else {
          return { content: [{ type: "text", text: "eldenring.exe is not running." }] };
        }
      } catch (e) {
        return { content: [{ type: "text", text: `Error checking status: ${e.message}` }] };
      }
    }

    if (name === "tail_log") {
      if (!fs.existsSync(LOG_FILE)) {
        return { content: [{ type: "text", text: "OptiScaler.log not found." }] };
      }
      
      const stats = fs.statSync(LOG_FILE);
      if (stats.size < lastTailPosition) {
        lastTailPosition = 0; // File was truncated or recreated
      }
      
      const buffer = Buffer.alloc(stats.size - lastTailPosition);
      const fd = fs.openSync(LOG_FILE, "r");
      fs.readSync(fd, buffer, 0, buffer.length, lastTailPosition);
      fs.closeSync(fd);
      
      lastTailPosition = stats.size;
      const newContent = buffer.toString("utf-8");
      
      return { content: [{ type: "text", text: newContent || "No new lines." }] };
    }

    if (name === "get_crash_info") {
      try {
        const cmd = "powershell -Command \"Get-EventLog -LogName Application -Source 'Application Error' -Newest 5 | Where-Object { $_.Message -match 'eldenring.exe' } | Select-Object -Property TimeGenerated, Message | ConvertTo-Json\"";
        const { stdout } = await execAsync(cmd);
        return { content: [{ type: "text", text: stdout.trim() || "No recent crashes found." }] };
      } catch (e) {
        return { content: [{ type: "text", text: `Error reading event log: ${e.message}` }] };
      }
    }

    throw new Error(`Tool not found: ${name}`);
  } catch (error) {
    return {
      isError: true,
      content: [{ type: "text", text: `Error: ${error.message}` }],
    };
  }
});

async function main() {
  const transport = new StdioServerTransport();
  await server.connect(transport);
  console.error("MCP Debug Server running on stdio");
}

main().catch(console.error);
