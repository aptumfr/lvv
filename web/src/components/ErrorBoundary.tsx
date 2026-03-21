import { Component } from 'react';
import type { ErrorInfo, ReactNode } from 'react';

interface Props {
  children: ReactNode;
}

interface State {
  error: Error | null;
}

export class ErrorBoundary extends Component<Props, State> {
  state: State = { error: null };

  static getDerivedStateFromError(error: Error): State {
    return { error };
  }

  componentDidCatch(error: Error, info: ErrorInfo) {
    console.error('LVV UI error:', error, info.componentStack);
  }

  render() {
    if (this.state.error) {
      return (
        <div className="h-screen bg-slate-900 flex items-center justify-center p-8">
          <div className="max-w-lg text-center">
            <h1 className="text-xl font-bold text-red-400 mb-4">Something went wrong</h1>
            <pre className="text-xs text-slate-400 bg-slate-800 p-4 rounded overflow-auto text-left max-h-64 mb-4">
              {this.state.error.message}
              {'\n\n'}
              {this.state.error.stack}
            </pre>
            <button
              onClick={() => this.setState({ error: null })}
              className="px-4 py-2 text-sm bg-blue-600 hover:bg-blue-500 text-white rounded"
            >
              Try again
            </button>
          </div>
        </div>
      );
    }
    return this.props.children;
  }
}
