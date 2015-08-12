
s2e = {
  kleeArgs = {
    "--state-shared-memory=true",
    "--flush-tbs-on-state-switch=true", -- Important for the correctness of TB flushing
    "--use-concolic-execution=true",
    "--use-dfs-search=true",
    "--enable-speculative-forking=false",
    "--end-solver=stp",
    "--debug-log-state-merge=false",

    "--fork-on-symbolic-read=false",
    "--fork-on-symbolic-write=false",

    '--use-cache=false',

    "--collect-memops-ranges=false",
    -- "--discretize-memory-addresses=true",

    "--debug-interp-detection=false",
    "--debug-interp-instructions=false",
    "--debug-low-level-scheduler=false",
  }
}

plugins = {
  "BaseInstructions", "InterpreterAnalyzer"
}

pluginsConfig = {}
