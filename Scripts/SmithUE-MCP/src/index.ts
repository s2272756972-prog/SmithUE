#!/usr/bin/env node
import { createRequire } from 'module';
import { McpServer } from '@modelcontextprotocol/sdk/server/mcp.js';
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js';

import { SmithUEClient } from './client.js';
import { registerTools } from './tools.js';

const require = createRequire(import.meta.url);
const pkg = require('../package.json') as { name: string; version: string };

async function main(): Promise<void> {
  const args = process.argv.slice(2);

  if (args[0] !== 'serve') {
    process.stderr.write('Usage: smithue serve\n');
    process.exit(1);
  }

  const host = process.env.SMITHUE_HOST ?? 'localhost';
  const port = parseInt(process.env.SMITHUE_PORT ?? '13721', 10);
  const clientName = process.env.SMITHUE_CLIENT_NAME ?? 'OpenCode';

  const server = new McpServer({ name: 'SmithUE', version: pkg.version });
  const client = new SmithUEClient(port, host);

  registerTools(server, client);

  // Try immediate registration, then start keepalive loop for reconnection
  const sessionId = await client.registerSession(clientName);
  if (sessionId) {
    process.stderr.write(`[SmithUE] Session registered: ${sessionId} (client: ${clientName})\n`);
  } else {
    process.stderr.write('[SmithUE] UE not reachable yet. Keepalive will retry.\n');
  }
  client.startKeepalive(clientName);

  const transport = new StdioServerTransport();

  const shutdown = async () => {
    process.stderr.write('[SmithUE] Shutting down...\n');
    await client.unregisterSession();
    await server.close();
    process.exit(0);
  };

  process.on('SIGINT', shutdown);
  process.on('SIGTERM', shutdown);

  await server.connect(transport);
}

main().catch((err) => {
  process.stderr.write(`[SmithUE] Fatal error: ${err instanceof Error ? err.message : String(err)}\n`);
  process.exit(1);
});
