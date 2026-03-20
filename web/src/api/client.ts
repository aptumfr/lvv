const BASE = '';

async function request<T>(path: string, options?: RequestInit): Promise<T> {
  const res = await fetch(`${BASE}${path}`, {
    headers: { 'Content-Type': 'application/json' },
    ...options,
  });
  if (!res.ok) {
    const text = await res.text();
    throw new Error(text || res.statusText);
  }
  return res.json();
}

export interface FindAtResult {
  found: boolean;
  name?: string;
  type?: string;
  auto_path?: string;
  text?: string;
  selector?: string;
  x?: number;
  y?: number;
  width?: number;
  height?: number;
  clickable?: boolean;
}

export interface VisualDiffResult {
  passed: boolean;
  identical: boolean;
  diff_percentage: number;
  diff_pixels: number;
  total_pixels: number;
  first_run?: boolean;
  message?: string;
}

export const api = {
  health: () => request<{ status: string; connected: boolean; streaming: boolean; clients: number }>('/api/health'),
  connect: () => request<{ connected: boolean }>('/api/connect', { method: 'POST', body: '{}' }),
  disconnect: () => request<{ disconnected: boolean }>('/api/disconnect', { method: 'POST' }),
  ping: () => request<{ version: string }>('/api/ping'),
  tree: () => request<any>('/api/tree'),
  screenshotUrl: () => `${BASE}/api/screenshot?t=${Date.now()}`,
  click: (name: string) => request<{ success: boolean }>('/api/click', { method: 'POST', body: JSON.stringify({ name }) }),
  clickAt: (x: number, y: number) => request<{ success: boolean }>('/api/click', { method: 'POST', body: JSON.stringify({ x, y }) }),
  press: (x: number, y: number) => request<{ success: boolean }>('/api/press', { method: 'POST', body: JSON.stringify({ x, y }) }),
  moveTo: (x: number, y: number) => request<{ success: boolean }>('/api/move', { method: 'POST', body: JSON.stringify({ x, y }) }),
  release: () => request<{ success: boolean }>('/api/release', { method: 'POST', body: '{}' }),
  typeText: (text: string) => request<{ success: boolean }>('/api/type', { method: 'POST', body: JSON.stringify({ text }) }),
  sendKey: (key: string) => request<{ success: boolean }>('/api/key', { method: 'POST', body: JSON.stringify({ key }) }),
  swipe: (x: number, y: number, x_end: number, y_end: number, duration: number) =>
    request<{ success: boolean }>('/api/swipe', { method: 'POST', body: JSON.stringify({ x, y, x_end, y_end, duration }) }),
  widget: (name: string) => request<any>(`/api/widget/${encodeURIComponent(name)}`),
  screenInfo: () => request<{ width: number; height: number; color_format: string }>('/api/screen-info'),
  find: (name: string) => request<FindAtResult>(`/api/find?name=${encodeURIComponent(name)}`),
  widgets: () => request<Array<{name: string; type: string; auto_path: string; visible: boolean; clickable: boolean}>>('/api/widgets'),
  findAt: (x: number, y: number) => request<FindAtResult>('/api/find-at', { method: 'POST', body: JSON.stringify({ x, y }) }),
  runCode: (code: string) => request<{ success: boolean; output: string }>('/api/test/run', { method: 'POST', body: JSON.stringify({ code }) }),
  runFiles: (files: string[]) => request<any>('/api/test/run', { method: 'POST', body: JSON.stringify({ files }) }),
  visualCompare: (reference: string, threshold?: number) =>
    request<VisualDiffResult>('/api/visual/compare', {
      method: 'POST',
      body: JSON.stringify({ reference, threshold: threshold ?? 0.1 }),
    }),
};
