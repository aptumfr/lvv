import { create } from 'zustand';
import type { WidgetInfo } from '../types';

interface AppState {
  connected: boolean;
  setConnected: (v: boolean) => void;

  widgetTree: WidgetInfo | null;
  setWidgetTree: (tree: WidgetInfo | null) => void;

  selectedWidget: WidgetInfo | null;
  setSelectedWidget: (w: WidgetInfo | null) => void;

  testCode: string;
  setTestCode: (code: string) => void;

  testOutput: string;
  setTestOutput: (output: string) => void;

  recording: boolean;
  setRecording: (v: boolean) => void;

  recordedSteps: string[];
  addRecordedStep: (step: string) => void;
  clearRecordedSteps: () => void;
}

export const useAppStore = create<AppState>((set) => ({
  connected: false,
  setConnected: (v) => set({ connected: v }),

  widgetTree: null,
  setWidgetTree: (tree) => set({ widgetTree: tree }),

  selectedWidget: null,
  setSelectedWidget: (w) => set({ selectedWidget: w }),

  testCode: '# Write your test here\nimport lvv\n\n# lvv.click("button_name")\n# lvv.assert_visible("widget_name")\n',
  setTestCode: (code) => set({ testCode: code }),

  testOutput: '',
  setTestOutput: (output) => set({ testOutput: output }),

  recording: false,
  setRecording: (v) => set({ recording: v }),

  recordedSteps: [],
  addRecordedStep: (step) => set((s) => ({ recordedSteps: [...s.recordedSteps, step] })),
  clearRecordedSteps: () => set({ recordedSteps: [] }),
}));
