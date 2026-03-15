export interface WidgetInfo {
  id: number;
  name: string;
  type: string;
  x: number;
  y: number;
  width: number;
  height: number;
  visible: boolean;
  clickable: boolean;
  auto_path: string;
  text: string;
  children?: WidgetInfo[];
}

export interface HealthStatus {
  status: string;
  connected: boolean;
  streaming: boolean;
  clients: number;
}

export interface TestResult {
  name: string;
  status: 'pass' | 'fail';
  duration: number;
  message: string;
  output: string;
}

export interface TestSuiteResult {
  passed: boolean;
  total: number;
  pass_count: number;
  fail_count: number;
  duration: number;
  tests: TestResult[];
}

export interface ScreenInfo {
  width: number;
  height: number;
  color_format: string;
}
