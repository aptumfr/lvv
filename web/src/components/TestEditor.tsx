import { useState } from 'react';
import { api } from '../api/client';
import { useAppStore } from '../store/appStore';

export function TestEditor() {
  const {
    testCode, setTestCode,
    testOutput, setTestOutput,
    recording, setRecording,
    recordedSteps, clearRecordedSteps,
    connected,
  } = useAppStore();
  const [running, setRunning] = useState(false);

  const handleRun = async () => {
    if (!connected) return;
    setRunning(true);
    setTestOutput('Running...\n');
    try {
      const res = await api.runCode(testCode);
      setTestOutput(
        `${res.success ? 'PASSED' : 'FAILED'}\n\n${res.output}`
      );
    } catch (e: any) {
      setTestOutput(`Error: ${e.message}`);
    }
    setRunning(false);
  };

  const toggleRecording = () => {
    if (!recording) {
      clearRecordedSteps();
    }
    setRecording(!recording);
  };

  const applyRecording = () => {
    const code = 'import lvv\n\n' + recordedSteps.join('\n') + '\n';
    setTestCode(code);
    setRecording(false);
  };

  return (
    <div className="flex flex-col h-full">
      <div className="flex items-center gap-2 p-2 border-b border-slate-700">
        <h2 className="text-sm font-semibold">Test Script</h2>
        <button
          onClick={handleRun}
          disabled={!connected || running}
          className="px-2 py-1 text-xs bg-green-600 hover:bg-green-500 rounded disabled:opacity-50"
        >
          {running ? 'Running...' : 'Run'}
        </button>
        <button
          onClick={toggleRecording}
          disabled={!connected}
          className={`px-2 py-1 text-xs rounded ${
            recording
              ? 'bg-red-600 hover:bg-red-500 animate-pulse'
              : 'bg-orange-600 hover:bg-orange-500'
          } disabled:opacity-50`}
        >
          {recording ? `Recording (${recordedSteps.length})` : 'Record'}
        </button>
        {recording && recordedSteps.length > 0 && (
          <button
            onClick={applyRecording}
            className="px-2 py-1 text-xs bg-blue-600 hover:bg-blue-500 rounded"
          >
            Apply
          </button>
        )}
      </div>
      <div className="flex-1 flex flex-col min-h-0">
        <textarea
          value={testCode}
          onChange={(e) => setTestCode(e.target.value)}
          className="flex-1 bg-slate-900 text-green-300 font-mono text-xs p-3 resize-none outline-none border-b border-slate-700"
          spellCheck={false}
          placeholder="import lvv&#10;&#10;lvv.click('button_name')&#10;lvv.assert_visible('widget')"
        />
        <div className="h-32 overflow-auto bg-slate-950 p-2">
          <pre className="text-xs text-slate-300 whitespace-pre-wrap font-mono">
            {testOutput || 'Output will appear here...'}
          </pre>
        </div>
      </div>
    </div>
  );
}
