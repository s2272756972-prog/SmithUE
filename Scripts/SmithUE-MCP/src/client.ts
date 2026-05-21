import type { SmithUEExecuteResponse, SmithUEToolSchema } from './types.js';

export class SmithUEClient {
  private host: string;
  private port: number;
  private timeout: number;
  private sessionId: string | null = null;
  private keepaliveTimer: ReturnType<typeof setInterval> | null = null;

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
      const headers: Record<string, string> = { 'Content-Type': 'application/json' };
      if (this.sessionId) {
        headers['X-SmithUE-Session'] = this.sessionId;
      }

      const response = await fetch(`${this.baseUrl}/api/v1/execute`, {
        method: 'POST',
        headers,
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

  async executeWithFailover(
    command: string,
    params: Record<string, unknown> = {}
  ): Promise<SmithUEExecuteResponse> {
    const syncTimeout = parseInt(process.env.SMITHUE_SYNC_TIMEOUT || "10000", 10);
    const asyncTimeout = parseInt(process.env.SMITHUE_ASYNC_TIMEOUT || "120000", 10);

    try {
      return await this.postExecute(command, params, syncTimeout);
    } catch (err) {
      if ((err as Error).name !== 'AbortError') {
        throw this.normalizeRequestError(err, command);
      }
    }

    const taskId = await this.startAsyncTask(command, params);
    const startedAt = Date.now();
    let delayMs = 500;

    while (Date.now() - startedAt <= asyncTimeout) {
      const remainingMs = asyncTimeout - (Date.now() - startedAt);
      if (remainingMs <= 0) break;

      const poll = await this.getAsyncTask(taskId, remainingMs);
      if (poll.status === 'error') {
        if (poll.error?.startsWith('Unknown task_id:')) {
          throw new Error('Task lost — server may have restarted');
        }
        throw new Error(poll.error ?? `SmithUE async task failed: ${command}`);
      }

      const data = poll.data;
      if (this.isAsyncTaskComplete(data)) {
        return { status: 'success', data: data.result };
      }

      await this.sleep(Math.min(delayMs, Math.max(asyncTimeout - (Date.now() - startedAt), 0)));
      delayMs = Math.min(delayMs * 2, 4000);
    }

    throw new Error(`Async task timed out after ${Math.ceil(asyncTimeout / 1000)}s`);
  }

  private async postExecute(
    command: string,
    params: Record<string, unknown>,
    timeoutMs: number
  ): Promise<SmithUEExecuteResponse> {
    const data = await this.postJson<SmithUEExecuteResponse>('/api/v1/execute', { command, params }, timeoutMs);

    if (data.status === 'error') {
      throw new Error(data.error ?? `SmithUE command failed: ${command}`);
    }

    return data;
  }

  private async startAsyncTask(command: string, params: Record<string, unknown>): Promise<string> {
    const data = await this.postJson<SmithUEExecuteResponse>('/api/v1/async', { command, params });

    if (data.status === 'error') {
      throw new Error(data.error ?? `SmithUE async task failed: ${command}`);
    }

    const taskId = data.data?.task_id;
    if (typeof taskId !== 'string' || taskId.length === 0) {
      throw new Error('SmithUE async task did not return a task_id');
    }

    return taskId;
  }

  private async getAsyncTask(taskId: string, timeoutMs: number): Promise<SmithUEExecuteResponse> {
    return this.getJson<SmithUEExecuteResponse>(`/api/v1/async/${encodeURIComponent(taskId)}`, timeoutMs);
  }

  private async postJson<T>(path: string, body: unknown, timeoutMs?: number): Promise<T> {
    return this.fetchJson<T>(path, {
      method: 'POST',
      headers: this.headers(),
      body: JSON.stringify(body),
    }, timeoutMs);
  }

  private async getJson<T>(path: string, timeoutMs?: number): Promise<T> {
    return this.fetchJson<T>(path, {
      method: 'GET',
      headers: this.headers(),
    }, timeoutMs);
  }

  private async fetchJson<T>(path: string, init: RequestInit, timeoutMs?: number): Promise<T> {
    const controller = timeoutMs !== undefined ? new AbortController() : null;
    const timer = controller ? setTimeout(() => controller.abort(), timeoutMs) : null;

    try {
      const response = await fetch(`${this.baseUrl}${path}`, {
        ...init,
        signal: controller?.signal,
      });

      if (!response.ok) {
        const body = await response.text().catch(() => '');
        throw new Error(
          `SmithUE plugin returned HTTP ${response.status}. Body: ${body.slice(0, 200)}`
        );
      }

      return (await response.json()) as T;
    } finally {
      if (timer) clearTimeout(timer);
    }
  }

  private headers(): Record<string, string> {
    const headers: Record<string, string> = { 'Content-Type': 'application/json' };
    if (this.sessionId) {
      headers['X-SmithUE-Session'] = this.sessionId;
    }
    return headers;
  }

  private normalizeRequestError(err: unknown, command: string): Error {
    const msg = (err as Error).message ?? '';
    if (
      msg.includes('ECONNREFUSED') ||
      msg.includes('fetch failed') ||
      msg.includes('Failed to fetch') ||
      msg.includes('ENOTFOUND') ||
      msg.includes('connect ECONNREFUSED')
    ) {
      return new Error(
        `SmithUE plugin unreachable at ${this.host}:${this.port}. Start UE Editor with SmithUE plugin enabled.`
      );
    }

    if ((err as Error).name === 'AbortError') {
      return new Error(`SmithUE plugin timed out. Command: ${command} (port: ${this.port})`);
    }

    return err instanceof Error ? err : new Error(String(err));
  }

  private isAsyncTaskComplete(data: Record<string, unknown> | undefined): data is { completed: true; result: Record<string, unknown> } {
    return data?.completed === true && this.isRecord(data.result);
  }

  private isRecord(value: unknown): value is Record<string, unknown> {
    return typeof value === 'object' && value !== null && !Array.isArray(value);
  }

  private async sleep(ms: number): Promise<void> {
    await new Promise<void>((resolve) => setTimeout(resolve, ms));
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

  /** Register this MCP server as a client session. */
  async registerSession(clientName: string): Promise<string | null> {
    try {
      const res = await this.request('register_session', { client_name: clientName });
      const data = res.data as { session_id: string };
      this.sessionId = data.session_id;
      return this.sessionId;
    } catch {
      return null;
    }
  }

  /** Unregister the session. Called on shutdown. */
  async unregisterSession(): Promise<void> {
    this.stopKeepalive();
    if (!this.sessionId) return;
    try {
      await this.request('unregister_session', { session_id: this.sessionId });
    } catch {
      // Best-effort — UE may already be gone
    }
    this.sessionId = null;
  }

  /**
   * Start a keepalive loop that handles all connection lifecycle:
   * - UE not up yet → keep retrying registration
   * - UE running, session valid → heartbeat via X-SmithUE-Session header
   * - UE restarted → detect stale session, re-register
   */
  startKeepalive(clientName: string, intervalMs = 8000): void {
    this.stopKeepalive();

    this.keepaliveTimer = setInterval(async () => {
      try {
        await this.ping();
        // UE is reachable — ensure we have a session
        if (!this.sessionId) {
          const id = await this.registerSession(clientName);
          if (id) {
            process.stderr.write(`[SmithUE] Session registered: ${id} (client: ${clientName})\n`);
          }
        }
      } catch {
        // UE is down — clear stale session so we re-register when it comes back
        if (this.sessionId) {
          process.stderr.write('[SmithUE] UE disconnected. Session cleared, will re-register.\n');
          this.sessionId = null;
        }
      }
    }, intervalMs);

    this.keepaliveTimer.unref();
  }

  private stopKeepalive(): void {
    if (this.keepaliveTimer) {
      clearInterval(this.keepaliveTimer);
      this.keepaliveTimer = null;
    }
  }

  getSessionId(): string | null {
    return this.sessionId;
  }
}
