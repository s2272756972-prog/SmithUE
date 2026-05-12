import type { McpServer } from '@modelcontextprotocol/sdk/server/mcp.js';
import { z } from 'zod';

import type { SmithUEClient } from './client.js';

const AVAILABLE_DOMAINS = 'System, Asset, Material, Editor, Blueprint, Viewport, Observation, Analysis';

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
    'smithue_execute',
    'Execute any SmithUE command in the UE5 editor. Use smithue_list_domain to discover available commands and their parameters first.',
    {
      command: z.string(),
      params: z.record(z.unknown()).optional(),
    },
    { destructiveHint: true, idempotentHint: false },
    async ({ command, params }) => {
      try {
        const result = await client.execute(command, params);
        return textResult(result);
      } catch (err) {
        return errorResult(err);
      }
    }
  );

  server.tool(
    'smithue_search',
    'Search available SmithUE commands by keyword. Returns command names, categories, and descriptions. Use smithue_list_domain with a domain name to get full parameter schemas.',
    {
      query: z.string(),
    },
    { readOnlyHint: true },
    async ({ query }) => {
      try {
        const tools = await client.listTools();
        const normalizedQuery = query.toLowerCase();
        const matches = tools
          .filter((tool) =>
            tool.name.includes(query) ||
            tool.description.toLowerCase().includes(normalizedQuery) ||
            tool.category.toLowerCase().includes(normalizedQuery)
          )
          .map(({ name, category, description }) => ({ name, category, description }));

        if (matches.length === 0) {
          return textResult(`No commands found matching '${query}'. Use smithue_list_domain() to see all available domains.`);
        }

        return textResult(matches);
      } catch (err) {
        return errorResult(err);
      }
    }
  );

  server.tool(
    'smithue_list_domain',
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
              params: params.map(({ name: paramName, type, description: paramDescription, required }) => ({
                name: paramName,
                type,
                description: paramDescription,
                required,
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
            hint: `Call smithue_list_domain('${domainName}') for full command list`,
          }))
        );
      } catch (err) {
        return errorResult(err);
      }
    }
  );
}
