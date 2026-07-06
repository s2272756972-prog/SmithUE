#!/usr/bin/env node
/**
 * scripts/regen-tools.mjs
 * Regenerates TOOLS.md from the live /api/v1/tools endpoint.
 *
 * Usage:  node scripts/regen-tools.mjs
 * Requires: UE Editor running with SmithUE plugin loaded (AIScript project).
 */

import { readFileSync, writeFileSync, readdirSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { get } from 'node:http';
import { fileURLToPath } from 'node:url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const REPO_ROOT = resolve(__dirname, '..');

// ---------------------------------------------------------------------------
// 1. Port discovery
// ---------------------------------------------------------------------------

/**
 * Scans %LOCALAPPDATA%\.smithue\*.port, filters for project_name === 'AIScript',
 * and returns the port from the most recently started instance.
 */
function discoverPort() {
  const overridePort = Number(process.env.SMITHUE_PORT);
  if (Number.isInteger(overridePort) && overridePort > 0) {
    return overridePort;
  }

  const smithueDir = join(process.env.LOCALAPPDATA, '.smithue');
  let best = null;
  for (const f of readdirSync(smithueDir).filter(x => x.endsWith('.port'))) {
    try {
      const data = JSON.parse(readFileSync(join(smithueDir, f), 'utf8'));
      if (data.project_name === 'AIScript') {
        if (!best || data.started_at > best.started_at) best = data;
      }
    } catch { /* skip malformed */ }
  }
  if (!best) {
    throw new Error(
      'No AIScript port file found — is the UE editor running with SmithUE loaded?'
    );
  }
  return best.port;
}

// ---------------------------------------------------------------------------
// 2. HTTP helper
// ---------------------------------------------------------------------------

/** Minimal HTTP GET returning parsed JSON. */
function httpGet(url) {
  return new Promise((res, rej) => {
    const req = get(url, (response) => {
      let raw = '';
      response.on('data', c => (raw += c));
      response.on('end', () => {
        if (response.statusCode !== 200) {
          rej(new Error(`HTTP ${response.statusCode} from ${url}: ${raw.slice(0, 200)}`));
          return;
        }
        try { res(JSON.parse(raw)); }
        catch (e) {
          rej(new Error(`JSON parse error: ${e.message}\n${raw.slice(0, 300)}`));
        }
      });
    });
    req.on('error', rej);
    req.setTimeout(30_000, () => req.destroy(new Error(`Request to ${url} timed out`)));
  });
}

// ---------------------------------------------------------------------------
// 3. Formatting
// ---------------------------------------------------------------------------

/**
 * Format a single tool block.
 *
 * Format (LF throughout):
 *   ### `<name>`\n
 *   \n
 *   <description>\n
 *   \n                          <- only when params exist
 *   **Parameters:**\n
 *   \n                          <- only when params exist
 *   - `<p>` (<type>[, required]): <description>\n
 *   ...
 *   \n                          <- separator appended for every tool except the last
 */
function formatTool(tool, isLast) {
  const params = tool.parameters ?? tool.params ?? [];
  let out = `### \`${tool.name}\`\n\n${tool.description}\n`;
  if (params.length > 0) {
    out += '\n**Parameters:**\n\n';
    for (const p of params) {
      const req = p.required ? ', required' : '';
      out += `- \`${p.name}\` (${p.type}${req}): ${p.description}\n`;
    }
  }
  if (!isLast) out += '\n'; // blank-line separator before next tool / section
  return out;
}

// ---------------------------------------------------------------------------
// 4. Main
// ---------------------------------------------------------------------------

async function main() {
  const port = discoverPort();
  process.stderr.write(`Connecting to port ${port}…\n`);

  const json = await httpGet(`http://127.0.0.1:${port}/api/v1/tools`);
  const tools = json.data.tools;
  process.stderr.write(`Fetched ${tools.length} tools\n`);

  // Group tools by category, preserving the API's within-category order.
  const catMap = new Map();
  for (const t of tools) {
    if (!catMap.has(t.category)) catMap.set(t.category, []);
    catMap.get(t.category).push(t);
  }

  // Sort categories alphabetically (matches existing TOOLS.md).
  const cats = [...catMap.keys()].sort();

  // Build file header.
  const header =
    `# SmithUE Tools Reference\n\n` +
    `> Generated from \`/api/v1/tools\`. Total: **${tools.length} tools** across **${cats.length} domains**.\n\n` +
    `---\n\n`;

  // Build section + tool content.
  let content = header;
  for (let si = 0; si < cats.length; si++) {
    const cat = cats[si];
    const catTools = catMap.get(cat);
    content += `## ${cat}\n\n`;
    for (let ti = 0; ti < catTools.length; ti++) {
      const isLast = si === cats.length - 1 && ti === catTools.length - 1;
      content += formatTool(catTools[ti], isLast);
    }
  }

  // Write TOOLS.md (LF line endings — file is stored as LF in the repo).
  const toolsPath = join(REPO_ROOT, 'TOOLS.md');
  writeFileSync(toolsPath, content, 'utf8');
  process.stderr.write(
    `TOOLS.md written: ${content.length} chars, ${tools.length} tools, ${cats.length} domains\n`
  );
}

main().catch(err => {
  process.stderr.write(`Error: ${err.message}\n`);
  process.exit(1);
});
