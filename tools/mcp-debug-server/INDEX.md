# MCP Debug Server

This directory contains an MCP server (`mcp-debug-server`) for debugging the OptiScaler fork in Elden Ring.

## Purpose
Provides tools to read and tail logs, query TAA status, edit configuration files, launch the game, check process status, deploy built DLLs, and check crash logs.

## Setup
- Run `npm install` to install dependencies (`@modelcontextprotocol/sdk` and `ini`).
- The server runs on standard input/output (stdio) and uses the main entry point `mcp_debug_server.js`.
