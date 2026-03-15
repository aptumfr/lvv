import { useState } from 'react';
import { ConnectionPanel } from './components/ConnectionPanel';
import { LiveView } from './components/LiveView';
import { WidgetTreePanel } from './components/WidgetTree';
import { PropertyPanel } from './components/PropertyPanel';
import { TestEditor } from './components/TestEditor';
import { VisualDiffViewer } from './components/VisualDiffViewer';

type BottomTab = 'script' | 'visual';

function App() {
  const [bottomTab, setBottomTab] = useState<BottomTab>('script');

  return (
    <div className="flex h-screen bg-slate-800 text-slate-200">
      {/* Left sidebar */}
      <div className="w-72 flex flex-col border-r border-slate-700 bg-slate-800">
        <div className="p-3 border-b border-slate-700">
          <h1 className="text-lg font-bold text-white">LVV</h1>
          <p className="text-xs text-slate-400">LVGL Test Automation</p>
        </div>
        <ConnectionPanel />
        <div className="flex-1 overflow-hidden flex flex-col">
          <div className="flex-1 overflow-hidden">
            <WidgetTreePanel />
          </div>
          <div className="h-48 border-t border-slate-700 overflow-auto">
            <PropertyPanel />
          </div>
        </div>
      </div>

      {/* Main content */}
      <div className="flex-1 flex flex-col">
        {/* Top: Live view */}
        <div className="flex-1 min-h-0">
          <LiveView />
        </div>

        {/* Bottom panel with tabs */}
        <div className="h-80 border-t border-slate-700 flex flex-col">
          <div className="flex border-b border-slate-700">
            <button
              onClick={() => setBottomTab('script')}
              className={`px-4 py-1.5 text-xs font-medium ${
                bottomTab === 'script'
                  ? 'bg-slate-700 text-white border-b-2 border-blue-400'
                  : 'text-slate-400 hover:text-white'
              }`}
            >
              Test Script
            </button>
            <button
              onClick={() => setBottomTab('visual')}
              className={`px-4 py-1.5 text-xs font-medium ${
                bottomTab === 'visual'
                  ? 'bg-slate-700 text-white border-b-2 border-blue-400'
                  : 'text-slate-400 hover:text-white'
              }`}
            >
              Visual Regression
            </button>
          </div>
          <div className="flex-1 overflow-hidden">
            {bottomTab === 'script' && <TestEditor />}
            {bottomTab === 'visual' && <VisualDiffViewer />}
          </div>
        </div>
      </div>
    </div>
  );
}

export default App;
