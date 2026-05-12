import type { SmithUEExecuteResponse, SmithUEToolSchema } from './types.js';

export class SmithUEClient {
  private host: string;
  private port: number;
  private timeout: number;

  constructor(port = 13721, host = 'localhost', timeout = 30000) {
    this.port = port;
    this.host = host;
    this.timeout = timeout;
  }

  private get baseUrl(): string {
    return `http://${this.host}:${this.port}`;
  }

  private async request(command: string, params: Record<string, unknown> = {}): Promise<SmithUEExecuteResponse> {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), this.timeout);

    try {
      const response = await fetch(this.baseUrl, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ command, params }),
        signal: controller.signal,
      });

      if (!response.ok) {
        const body = await response.text().catch(() => '');
        throw new Error(
          `SmithUE plugin returned HTTP ${response.status}. Body: ${body.slice(0, 200)}`
        );
      }

      const data = (await response.json()) as SmithUEExecuteResponse;

      if (data.status === 'error') {
        throw new Error(data.error ?? `SmithUE command failed: ${command}`);
      }

      return data;
    } catch (err) {
      if ((err as Error).name === 'AbortError') {
        throw new Error(
          `SmithUE plugin timed out after 30s. Command: ${command} (port: ${this.port})`
        );
      }

      const msg = (err as Error).message ?? '';
      if (
        msg.includes('ECONNREFUSED') ||
        msg.includes('fetch failed') ||
        msg.includes('Failed to fetch') ||
        msg.includes('ENOTFOUND') ||
        msg.includes('connect ECONNREFUSED')
      ) {
        throw new Error(
          `SmithUE plugin unreachable at ${this.host}:${this.port}. Start UE Editor with SmithUE plugin enabled.`
        );
      }

      throw err;
    } finally {
      clearTimeout(timer);
    }
  }

  async ping(): Promise<{ message: string }> {
    const res = await this.request('ping', {});
    return res.data as { message: string };
  }

  async execute(command: string, params: Record<string, unknown> = {}): Promise<SmithUEExecuteResponse> {
    return this.request(command, params);
  }

  async listTools(category?: string): Promise<SmithUEToolSchema[]> {
    const res = await this.execute('list_tools', category ? { category } : {});
    const data = res.data as { protocol_version: string; tools: SmithUEToolSchema[] };
    return data.tools;
  }

  async isConnected(): Promise<boolean> {
    try {
      await this.ping();
      return true;
    } catch {
      return false;
    }
  }
}
