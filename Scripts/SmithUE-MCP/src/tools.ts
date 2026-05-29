import type { McpServer } from '@modelcontextprotocol/sdk/server/mcp.js';
import { z } from 'zod';

import type { SmithUEClient } from './client.js';

const AVAILABLE_DOMAINS = 'System, Asset, Material, Editor, Blueprint, Viewport, Observation, Analysis';

const SEARCH_ALIASES: Record<string, string[]> = {
  'input': ['key', 'keyboard', 'bind', 'mapping', 'action', 'enhanced'],
  'blueprint': ['bp', 'graph', 'node', 'k2'],
  'create': ['add', 'new', 'spawn', 'make'],
  'connect': ['wire', 'link', 'pin'],
  'delete': ['remove', 'destroy'],
  'search': ['find', 'query', 'list'],
};

function textResult(result: unknown) {
  return {
    content: [{ type: 'text' as const, text: JSON.stringify(result, null, 2) }],
  };
}

function errorResult(err: unknown) {
  const message = err instanceof Error ? err.message : String(err);
  return {
    content: [{ type: 'text' as const, text: `Error: ${message}` }],
    isError: true,
  };
}

export function registerTools(server: McpServer, client: SmithUEClient): void {
  server.tool(
    'execute',
    'Execute any SmithUE command in the UE5 editor. Use list_domain to discover available commands and their parameters first.',
    {
      command: z.string(),
      params: z.record(z.unknown()).optional(),
    },
    { destructiveHint: true, idempotentHint: false },
    async ({ command, params }) => {
      try {
        // Pre-flight schema validation
        const allTools = await client.listTools();
        const toolSchema = allTools.find((t) => t.name === command);

        if (toolSchema) {
          const args = params ?? {};
          const filled: Record<string, unknown> = { ...args };

          for (const p of toolSchema.params) {
            const provided = filled[p.name];
            if (provided === undefined) {
              if (p.required) {
                if (p.default) {
                  filled[p.name] = p.default;
                } else {
                  return errorResult(new Error(`Validation failed: param '${p.name}' is required but missing`));
                }
              }
            } else if (p.allowedValues && p.allowedValues.length > 0) {
              if (!p.allowedValues.includes(String(provided))) {
                return errorResult(new Error(`Validation failed: param '${p.name}' value '${String(provided)}' not in allowed values [${p.allowedValues.join(', ')}]`));
              }
            }
          }

          const result = await client.executeWithFailover(command, filled);
          return textResult(result);
        }

        // No schema found — pass through as-is
        const result = await client.executeWithFailover(command, params);
        return textResult(result);
      } catch (err) {
        return errorResult(err);
      }
    }
  );

  server.tool(
    'search',
    'Search available SmithUE commands by keyword. Returns command names, categories, and descriptions. Use list_domain with a domain name to get full parameter schemas.',
    {
      query: z.string(),
      limit: z.number().int().min(1).max(50).optional(),
    },
    { readOnlyHint: true },
    async ({ query, limit = 10 }) => {
      try {
        if (!query) {
          return textResult(`No commands found matching ''. Use list_domain() to see all available domains.`);
        }

        const tools = await client.listTools();

        // Expand a single word with its aliases (bidirectional)
        function expandWord(word: string): string[] {
          const terms = new Set<string>([word]);
          // Direct: word -> aliases
          if (SEARCH_ALIASES[word]) {
            for (const alias of SEARCH_ALIASES[word]) terms.add(alias);
          }
          // Reverse: alias -> key
          for (const [key, aliases] of Object.entries(SEARCH_ALIASES)) {
            if (aliases.includes(word)) {
              terms.add(key);
              for (const alias of aliases) terms.add(alias);
            }
          }
          return Array.from(terms);
        }

        const words = query.toLowerCase().split(/\s+/).filter(Boolean);
        const expandedWords = words.map(expandWord);

        const scored = tools
          .map((tool) => {
            const paramText = tool.params
              .map((p) => `${p.name} ${p.description ?? ''}`)
              .join(' ')
              .toLowerCase();
            const searchText = [
              tool.name.toLowerCase(),
              tool.category.toLowerCase(),
              tool.description.toLowerCase(),
              paramText,
            ].join(' ');

            // ALL words must match (intersection): each word's expanded set must have at least one hit
            const allMatch = expandedWords.every((terms: string[]) =>
              terms.some((term: string) => searchText.includes(term))
            );
            if (!allMatch) return { tool, score: 0 };

            // Score: reward name/category matches higher
            let score = 0;
            const nameLower = tool.name.toLowerCase();
            const catLower = tool.category.toLowerCase();
            const descLower = tool.description.toLowerCase();
            for (const terms of expandedWords) {
              if (terms.some((t: string) => nameLower === t)) score += 3;
              else if (terms.some((t: string) => nameLower.includes(t))) score += 2;
              if (terms.some((t: string) => catLower.includes(t))) score += 1.5;
              if (terms.some((t: string) => descLower.includes(t))) score += 1;
              if (terms.some((t: string) => paramText.includes(t))) score += 0.5;
            }
            return { tool, score };
          })
          .filter(({ score }) => score > 0)
          .sort((a, b) => b.score - a.score)
          .slice(0, Math.min(limit, 50))
          .map(({ tool: { name, category, description } }) => ({ name, category, description }));

        if (scored.length === 0) {
          return textResult(`No commands found matching '${query}'. Use list_domain() to see all available domains.`);
        }

        return textResult(scored);
      } catch (err) {
        return errorResult(err);
      }
    }
  );

  server.tool(
    'list_domain',
    `List SmithUE commands by domain. Without arguments: returns all domain names with command counts. With domain argument: returns full command list with parameter schemas for that domain. Available domains: ${AVAILABLE_DOMAINS}.`,
    {
      domain: z.string().optional(),
    },
    { readOnlyHint: true },
    async ({ domain }) => {
      try {
        if (domain) {
          const tools = await client.listTools(domain);

          if (tools.length === 0) {
            return textResult(`No commands found in domain '${domain}'. Available domains: ${AVAILABLE_DOMAINS}`);
          }

          return textResult(
            tools.map(({ name, category, description, params }) => ({
              name,
              category,
              description,
              params: params.map((p) => ({
                name: p.name,
                type: p.type,
                description: p.description,
                required: p.required,
                ...(p.default ? { default: p.default } : {}),
                ...(p.itemsType ? { itemsType: p.itemsType } : {}),
                ...(p.allowedValues && p.allowedValues.length > 0 ? { allowedValues: p.allowedValues } : {}),
              })),
            }))
          );
        }

        const tools = await client.listTools();
        const counts = new Map<string, number>();

        for (const tool of tools) {
          counts.set(tool.category, (counts.get(tool.category) ?? 0) + 1);
        }

            return textResult(
              Array.from(counts.entries()).map(([domainName, count]) => ({
                domain: domainName,
                count,
                hint: `Call list_domain('${domainName}') for full command list`,
              }))
            );
      } catch (err) {
        return errorResult(err);
      }
    }
  );
}
