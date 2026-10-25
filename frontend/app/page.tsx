export default function HomePage() {
  return (
    <div className="max-w-5xl mx-auto py-16 text-center">
      <h1 className="text-5xl font-extrabold text-white mb-6">Register-Based Bytecode VM & GC</h1>
      <p className="text-slate-400 text-lg mb-8">Register-based bytecode virtual machine with closures, upvalues, generational mark-sweep GC, and WASM playground.</p>
      <a href="/dashboard" className="px-6 py-3 rounded-lg bg-cyan-400 text-black font-semibold">Open Telemetry Dashboard</a>
    </div>
  )
}
