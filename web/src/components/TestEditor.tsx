import { useRef, useState, useEffect } from 'react';
import { api } from '../api/client';
import { useAppStore } from '../store/appStore';

export function TestEditor() {
  const {
    testCode, setTestCode,
    testOutput, setTestOutput,
    recording, setRecording,
    recordedSteps, addRecordedStep, clearRecordedSteps,
    connected, selectedWidget,
  } = useAppStore();
  const [running, setRunning] = useState(false);
  const textareaRef = useRef<HTMLTextAreaElement>(null);
  const gutterRef = useRef<HTMLDivElement>(null);
  const stepsEndRef = useRef<HTMLDivElement>(null);

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
    clearRecordedSteps();
    setRecording(false);
  };

  const appendRecording = () => {
    const suffix = '\n' + recordedSteps.join('\n') + '\n';
    setTestCode(testCode.trimEnd() + suffix);
    clearRecordedSteps();
    setRecording(false);
  };

  const insertAssert = () => {
    if (selectedWidget) {
      const name = selectedWidget.name || selectedWidget.auto_path;
      if (name) {
        addRecordedStep(`lvv.assert_visible("${name}")`);
      }
    }
  };

  const insertWait = () => {
    addRecordedStep('lvv.wait(500)');
  };

  const insertWaitFor = () => {
    if (selectedWidget) {
      const name = selectedWidget.name || selectedWidget.auto_path;
      if (name) {
        addRecordedStep(`lvv.wait_for("${name}", 2000)`);
      }
    }
  };

  const insertScreenshot = () => {
    addRecordedStep('lvv.screenshot_compare("checkpoint.png", 0.5)');
  };

  const syncScroll = () => {
    if (gutterRef.current && textareaRef.current) {
      gutterRef.current.scrollTop = textareaRef.current.scrollTop;
    }
  };

  // Auto-scroll recorded steps
  useEffect(() => {
    stepsEndRef.current?.scrollIntoView({ behavior: 'smooth' });
  }, [recordedSteps.length]);

  const lineCount = Math.max(testCode.split('\n').length, 1);
  const lineNumbers = Array.from({ length: lineCount }, (_, i) => i + 1);

  const showRecordingPanel = recording;

  return (
    <div className="flex flex-col h-full">
      <div className="flex items-center gap-2 p-2 border-b border-slate-700 flex-wrap">
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
          {recording ? `Stop (${recordedSteps.length})` : 'Record'}
        </button>
        {recording && (
          <>
            <button
              onClick={insertAssert}
              disabled={!selectedWidget}
              className="px-2 py-1 text-xs bg-purple-600 hover:bg-purple-500 rounded disabled:opacity-50"
              title="Insert assert_visible for selected widget"
            >
              +Assert
            </button>
            <button
              onClick={insertWaitFor}
              disabled={!selectedWidget}
              className="px-2 py-1 text-xs bg-purple-600 hover:bg-purple-500 rounded disabled:opacity-50"
              title="Insert wait_for for selected widget"
            >
              +WaitFor
            </button>
            <button
              onClick={insertWait}
              className="px-2 py-1 text-xs bg-slate-600 hover:bg-slate-500 rounded"
              title="Insert a 500ms wait"
            >
              +Wait
            </button>
            <button
              onClick={insertScreenshot}
              className="px-2 py-1 text-xs bg-slate-600 hover:bg-slate-500 rounded"
              title="Insert screenshot comparison"
            >
              +Screenshot
            </button>
          </>
        )}
        {!recording && recordedSteps.length > 0 && (
          <>
            <button
              onClick={applyRecording}
              className="px-2 py-1 text-xs bg-blue-600 hover:bg-blue-500 rounded"
              title="Replace editor content with recording"
            >
              Replace
            </button>
            <button
              onClick={appendRecording}
              className="px-2 py-1 text-xs bg-blue-600 hover:bg-blue-500 rounded"
              title="Append recording to editor content"
            >
              Append
            </button>
            <button
              onClick={clearRecordedSteps}
              className="px-2 py-1 text-xs bg-slate-600 hover:bg-slate-500 rounded"
              title="Discard recorded steps"
            >
              Discard
            </button>
          </>
        )}
      </div>
      <div className="flex-1 flex flex-col min-h-0">
        {/* Editor always visible */}
        <div className="flex-1 flex min-h-0 border-b border-slate-700">
          <div
            ref={gutterRef}
            className="bg-slate-950 text-slate-500 text-xs font-mono text-right select-none overflow-hidden shrink-0"
            style={{ padding: '12px 8px 12px 8px', lineHeight: '1.35rem', width: '3rem' }}
          >
            {lineNumbers.map((n) => (
              <div key={n}>{n}</div>
            ))}
          </div>
          <textarea
            ref={textareaRef}
            value={testCode}
            onChange={(e) => setTestCode(e.target.value)}
            onScroll={syncScroll}
            className="flex-1 bg-slate-900 text-green-300 font-mono text-xs resize-none outline-none"
            style={{ padding: '12px 12px 12px 8px', lineHeight: '1.35rem' }}
            spellCheck={false}
            placeholder="import lvv&#10;&#10;lvv.click('button_name')&#10;lvv.assert_visible('widget')"
          />
        </div>
        {/* Bottom panel: recorded steps or output */}
        <div className="h-32 overflow-auto bg-slate-950 p-2">
          {showRecordingPanel ? (
            <div className="font-mono text-xs">
              {recordedSteps.map((step, i) => (
                <div key={i} className={
                  step.startsWith('lvv.wait') ? 'text-slate-500' :
                  step.startsWith('lvv.assert') ? 'text-yellow-300' :
                  step.startsWith('lvv.screenshot') ? 'text-cyan-300' :
                  'text-green-300'
                }>
                  {step}
                </div>
              ))}
              <div ref={stepsEndRef} />
              {recording && recordedSteps.length === 0 && (
                <div className="text-slate-600 animate-pulse">
                  Click on the live view to record...
                </div>
              )}
            </div>
          ) : (
            <pre className="text-xs text-slate-300 whitespace-pre-wrap font-mono">
              {testOutput || 'Output will appear here...'}
            </pre>
          )}
        </div>
      </div>
    </div>
  );
}
