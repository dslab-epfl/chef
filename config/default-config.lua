s2e = {
  kleeArgs = {
    "--state-shared-memory=true",
    "--flush-tbs-on-state-switch=false",
    "--use-concolic-execution=true",
    "--use-dfs-search=true",
    "--enable-speculative-forking=false",
    "--end-solver=stp",
    "--debug-log-state-merge=true"
--    "--use-cache=false",
--    "--use-query-pc-log=true"
  }
}

plugins = {
  "BaseInstructions",
  "MergingSearcher"
  --"ExecutionTracer",
  --"MemoryTracer",
  --"InterpreterMonitor",
  --"ConcolicSession"
}

pluginsConfig = {}
