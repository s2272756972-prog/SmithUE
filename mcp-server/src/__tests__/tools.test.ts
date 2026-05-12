import { describe, it, expect, vi, beforeEach } from 'vitest';
import type { SmithUEToolSchema } from '../types.js';

// Mock tool data for testing
const mockTools: SmithUEToolSchema[] = [
  {
    name: 'create_material',
    category: 'Material',
    description: 'Creates a new material asset',
    params: [
      { name: 'path', type: 'string', description: 'Asset path', required: true },
      { name: 'name', type: 'string', description: 'Material name', required: true },
    ],
  },
  {
    name: 'compile_material',
    category: 'Material',
    description: 'Compiles a material',
    params: [{ name: 'material_path', type: 'string', description: 'Path to material', required: true }],
  },
  {
    name: 'ping',
    category: 'System',
    description: 'Health check ping',
    params: [],
  },
  {
    name: 'create_blueprint',
    category: 'Blueprint',
    description: 'Creates a blueprint class',
    params: [{ name: 'class_name', type: 'string', description: 'Blueprint class name', required: true }],
  },
  {
    name: 'list_assets',
    category: 'Asset',
    description: 'Lists assets in a folder',
    params: [{ name: 'folder_path', type: 'string', description: 'Folder path', required: true }],
  },
];

// Helper function to filter tools (extracted from tools.ts logic)
function filterTools(tools: SmithUEToolSchema[], query: string): SmithUEToolSchema[] {
  const normalizedQuery = query.toLowerCase();
  return tools.filter(
    (tool) =>
      tool.name.includes(query) ||
      tool.description.toLowerCase().includes(normalizedQuery) ||
      tool.category.toLowerCase().includes(normalizedQuery)
  );
}

// Helper function to aggregate domains
function aggregateDomains(tools: SmithUEToolSchema[]): Record<string, number> {
  const domains: Record<string, number> = {};
  for (const tool of tools) {
    domains[tool.category] = (domains[tool.category] ?? 0) + 1;
  }
  return domains;
}

describe('Search Filtering', () => {
  it('should filter tools by query matching name', () => {
    const results = filterTools(mockTools, 'create_material');
    expect(results).toHaveLength(1);
    expect(results[0].name).toBe('create_material');
  });

  it('should filter tools by query matching category', () => {
    const results = filterTools(mockTools, 'material');
    expect(results).toHaveLength(2);
    expect(results.map((t) => t.name)).toEqual(['create_material', 'compile_material']);
  });

  it('should filter tools by query matching description', () => {
    const results = filterTools(mockTools, 'health');
    expect(results).toHaveLength(1);
    expect(results[0].name).toBe('ping');
  });

  it('should be case-insensitive', () => {
    const results = filterTools(mockTools, 'MATERIAL');
    expect(results).toHaveLength(2);
  });

  it('should return empty array for non-existent query', () => {
    const results = filterTools(mockTools, 'nonexistent');
    expect(results).toHaveLength(0);
  });

  it('should match partial names', () => {
    const results = filterTools(mockTools, 'create');
    expect(results).toHaveLength(2);
    expect(results.map((t) => t.name)).toEqual(['create_material', 'create_blueprint']);
  });

  it('should match ping by exact name', () => {
    const results = filterTools(mockTools, 'ping');
    expect(results).toHaveLength(1);
    expect(results[0].name).toBe('ping');
  });
});

describe('Domain Aggregation', () => {
  it('should count tools by domain', () => {
    const domains = aggregateDomains(mockTools);
    expect(domains).toEqual({
      Material: 2,
      System: 1,
      Blueprint: 1,
      Asset: 1,
    });
  });

  it('should handle empty tool list', () => {
    const domains = aggregateDomains([]);
    expect(domains).toEqual({});
  });

  it('should handle single domain', () => {
    const singleDomain = [mockTools[0]];
    const domains = aggregateDomains(singleDomain);
    expect(domains).toEqual({ Material: 1 });
  });

  it('should handle multiple tools in same domain', () => {
    const materialTools = mockTools.filter((t) => t.category === 'Material');
    const domains = aggregateDomains(materialTools);
    expect(domains).toEqual({ Material: 2 });
  });
});

describe('Domain Filtering', () => {
  it('should filter tools by domain', () => {
    const materialTools = mockTools.filter((t) => t.category === 'Material');
    expect(materialTools).toHaveLength(2);
    expect(materialTools.every((t) => t.category === 'Material')).toBe(true);
  });

  it('should return tools with full params for domain', () => {
    const materialTools = mockTools.filter((t) => t.category === 'Material');
    const firstTool = materialTools[0];
    expect(firstTool.params).toBeDefined();
    expect(firstTool.params.length).toBeGreaterThan(0);
  });

  it('should return empty array for non-existent domain', () => {
    const nonExistentTools = mockTools.filter((t) => t.category === 'NonExistent');
    expect(nonExistentTools).toHaveLength(0);
  });

  it('should preserve tool structure when filtering by domain', () => {
    const systemTools = mockTools.filter((t) => t.category === 'System');
    const tool = systemTools[0];
    expect(tool).toHaveProperty('name');
    expect(tool).toHaveProperty('category');
    expect(tool).toHaveProperty('description');
    expect(tool).toHaveProperty('params');
  });
});

describe('Tool Schema Validation', () => {
  it('should have required properties on all tools', () => {
    for (const tool of mockTools) {
      expect(tool).toHaveProperty('name');
      expect(tool).toHaveProperty('category');
      expect(tool).toHaveProperty('description');
      expect(tool).toHaveProperty('params');
      expect(typeof tool.name).toBe('string');
      expect(typeof tool.category).toBe('string');
      expect(typeof tool.description).toBe('string');
      expect(Array.isArray(tool.params)).toBe(true);
    }
  });

  it('should have valid param structure', () => {
    const toolWithParams = mockTools.find((t) => t.params.length > 0);
    expect(toolWithParams).toBeDefined();
    if (toolWithParams) {
      for (const param of toolWithParams.params) {
        expect(param).toHaveProperty('name');
        expect(param).toHaveProperty('type');
        expect(param).toHaveProperty('description');
        expect(param).toHaveProperty('required');
      }
    }
  });
});
