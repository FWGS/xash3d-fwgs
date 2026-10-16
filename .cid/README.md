# CID pipelines for xash3d-gc

Pipelines run on a self-hosted `cid-runner` on the owner's machine. Start
`cid serve` and `cid-runner` there, then set `HL1_GC_DIR`,
`HL1_GC_BUILD_COMMAND` and `HL1_GC_TEST_COMMAND` in the runner environment.
