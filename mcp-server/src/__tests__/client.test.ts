import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { SmithUEClient } from '../client.js';
import type { SmithUEExecuteResponse } from '../types.js';

describe('SmithUEClient', () => {
  let client: SmithUEClient;
  let fetchMock: ReturnType<typeof vi.fn>;

  beforeEach(() => {
    client = new SmithUEClient(13721, 'localhost', 30000);
    fetchMock = vi.fn();
    vi.stubGlobal('fetch', fetchMock);
  });

  afterEach(() => {
    vi.unstubAllGlobals();
  });

  describe('ping()', () => {
    it('should successfully ping the server', async () => {
      const mockResponse: SmithUEExecuteResponse = {
        status: 'success',
        data: { message: 'pong' },
      };

      fetchMock.mockResolvedValueOnce({
        ok: true,
        json: async () => mockResponse,
      });

      const result = await client.ping();
      expect(result).toEqual({ message: 'pong' });
      expect(fetchMock).toHaveBeenCalledOnce();
    });

    it('should throw error when server is unreachable (ECONNREFUSED)', async () => {
      const error = new Error('fetch failed: ECONNREFUSED');
      fetchMock.mockRejectedValueOnce(error);

      await expect(client.ping()).rejects.toThrow('SmithUE plugin unreachable');
    });

    it('should throw error when server is unreachable (ENOTFOUND)', async () => {
      const error = new Error('ENOTFOUND: getaddrinfo');
      fetchMock.mockRejectedValueOnce(error);

      await expect(client.ping()).rejects.toThrow('SmithUE plugin unreachable');
    });

    it('should throw error when server returns error status', async () => {
      const mockResponse: SmithUEExecuteResponse = {
        status: 'error',
        error: 'Server error',
      };

      fetchMock.mockResolvedValueOnce({
        ok: true,
        json: async () => mockResponse,
      });

      await expect(client.ping()).rejects.toThrow('Server error');
    });
  });

  describe('execute()', () => {
    it('should execute command successfully', async () => {
      const mockResponse: SmithUEExecuteResponse = {
        status: 'success',
        data: { result: 'command executed' },
      };

      fetchMock.mockResolvedValueOnce({
        ok: true,
        json: async () => mockResponse,
      });

      const result = await client.execute('test_command', { param1: 'value1' });
      expect(result).toEqual(mockResponse);
      expect(fetchMock).toHaveBeenCalledOnce();
    });

    it('should throw error when response status is error', async () => {
      const mockResponse: SmithUEExecuteResponse = {
        status: 'error',
        error: 'Command not found',
      };

      fetchMock.mockResolvedValueOnce({
        ok: true,
        json: async () => mockResponse,
      });

      await expect(client.execute('invalid_command')).rejects.toThrow('Command not found');
    });

    it('should throw error when HTTP response is not ok', async () => {
      fetchMock.mockResolvedValueOnce({
        ok: false,
        status: 500,
        text: async () => 'Internal Server Error',
      });

      await expect(client.execute('test_command')).rejects.toThrow('HTTP 500');
    });

    it('should handle connection refused error', async () => {
      const error = new Error('connect ECONNREFUSED 127.0.0.1:13721');
      fetchMock.mockRejectedValueOnce(error);

      await expect(client.execute('test_command')).rejects.toThrow('SmithUE plugin unreachable');
    });

    it('should handle Failed to fetch error', async () => {
      const error = new Error('Failed to fetch');
      fetchMock.mockRejectedValueOnce(error);

      await expect(client.execute('test_command')).rejects.toThrow('SmithUE plugin unreachable');
    });

    it('should pass params to request', async () => {
      const mockResponse: SmithUEExecuteResponse = {
        status: 'success',
        data: { result: 'ok' },
      };

      fetchMock.mockResolvedValueOnce({
        ok: true,
        json: async () => mockResponse,
      });

      const params = { key: 'value', number: 42 };
      await client.execute('test_command', params);

      expect(fetchMock).toHaveBeenCalledOnce();
      const callArgs = fetchMock.mock.calls[0];
      const body = JSON.parse(callArgs[1].body);
      expect(body.command).toBe('test_command');
      expect(body.params).toEqual(params);
    });
  });

  describe('listTools()', () => {
    it('should list all tools without category filter', async () => {
      const mockResponse: SmithUEExecuteResponse = {
        status: 'success',
        data: {
          protocol_version: '1.0',
          tools: [
            {
              name: 'create_material',
              category: 'Material',
              description: 'Creates a material',
              params: [],
            },
            {
              name: 'ping',
              category: 'System',
              description: 'Health check',
              params: [],
            },
          ],
        },
      };

      fetchMock.mockResolvedValueOnce({
        ok: true,
        json: async () => mockResponse,
      });

      const tools = await client.listTools();
      expect(tools).toHaveLength(2);
      expect(tools[0].name).toBe('create_material');
      expect(tools[1].name).toBe('ping');
    });

    it('should list tools filtered by category', async () => {
      const mockResponse: SmithUEExecuteResponse = {
        status: 'success',
        data: {
          protocol_version: '1.0',
          tools: [
            {
              name: 'create_material',
              category: 'Material',
              description: 'Creates a material',
              params: [
                {
                  name: 'path',
                  type: 'string',
                  description: 'Asset path',
                  required: true,
                },
              ],
            },
          ],
        },
      };

      fetchMock.mockResolvedValueOnce({
        ok: true,
        json: async () => mockResponse,
      });

      const tools = await client.listTools('Material');
      expect(tools).toHaveLength(1);
      expect(tools[0].category).toBe('Material');
      expect(tools[0].params).toHaveLength(1);

      // Verify category was passed in params
      const callArgs = fetchMock.mock.calls[0];
      const body = JSON.parse(callArgs[1].body);
      expect(body.params).toEqual({ category: 'Material' });
    });

    it('should return empty array when no tools found', async () => {
      const mockResponse: SmithUEExecuteResponse = {
        status: 'success',
        data: {
          protocol_version: '1.0',
          tools: [],
        },
      };

      fetchMock.mockResolvedValueOnce({
        ok: true,
        json: async () => mockResponse,
      });

      const tools = await client.listTools('NonExistent');
      expect(tools).toHaveLength(0);
    });

    it('should throw error when list_tools command fails', async () => {
      const mockResponse: SmithUEExecuteResponse = {
        status: 'error',
        error: 'Failed to list tools',
      };

      fetchMock.mockResolvedValueOnce({
        ok: true,
        json: async () => mockResponse,
      });

      await expect(client.listTools()).rejects.toThrow('Failed to list tools');
    });
  });

  describe('Error Handling', () => {
    it('should handle timeout error', async () => {
      const abortError = new Error('Aborted');
      abortError.name = 'AbortError';
      fetchMock.mockRejectedValueOnce(abortError);

      await expect(client.execute('test_command')).rejects.toThrow('timed out');
    });

    it('should handle JSON parse error gracefully', async () => {
      fetchMock.mockResolvedValueOnce({
        ok: true,
        json: async () => {
          throw new Error('Invalid JSON');
        },
      });

      await expect(client.execute('test_command')).rejects.toThrow();
    });

    it('should handle HTTP error with body', async () => {
      fetchMock.mockResolvedValueOnce({
        ok: false,
        status: 400,
        text: async () => 'Bad Request: invalid parameter',
      });

      await expect(client.execute('test_command')).rejects.toThrow('HTTP 400');
    });

    it('should handle HTTP error without body', async () => {
      fetchMock.mockResolvedValueOnce({
        ok: false,
        status: 503,
        text: async () => {
          throw new Error('Cannot read body');
        },
      });

      await expect(client.execute('test_command')).rejects.toThrow('HTTP 503');
    });

    it('should include port in unreachable error message', async () => {
      const error = new Error('ECONNREFUSED');
      fetchMock.mockRejectedValueOnce(error);

      await expect(client.execute('test_command')).rejects.toThrow('13721');
    });
  });

  describe('Request Construction', () => {
    it('should use correct base URL', async () => {
      const mockResponse: SmithUEExecuteResponse = {
        status: 'success',
        data: {},
      };

      fetchMock.mockResolvedValueOnce({
        ok: true,
        json: async () => mockResponse,
      });

      await client.execute('test');

      const url = fetchMock.mock.calls[0][0];
      expect(url).toBe('http://localhost:13721');
    });

    it('should set correct headers', async () => {
      const mockResponse: SmithUEExecuteResponse = {
        status: 'success',
        data: {},
      };

      fetchMock.mockResolvedValueOnce({
        ok: true,
        json: async () => mockResponse,
      });

      await client.execute('test');

      const options = fetchMock.mock.calls[0][1];
      expect(options.method).toBe('POST');
      expect(options.headers['Content-Type']).toBe('application/json');
    });

    it('should use custom port and host', async () => {
      const customClient = new SmithUEClient(9999, '192.168.1.1', 30000);
      const mockResponse: SmithUEExecuteResponse = {
        status: 'success',
        data: {},
      };

      fetchMock.mockResolvedValueOnce({
        ok: true,
        json: async () => mockResponse,
      });

      await customClient.execute('test');

      const url = fetchMock.mock.calls[0][0];
      expect(url).toBe('http://192.168.1.1:9999');
    });
  });
});
