# Hardening checkpoint

This checkpoint records audit progress against dev commit
`8fc2f6eab48fdd0bdb8e3e1221d9c3fc2d1e35fc`. No product fixes were made.

## Completed verification

The retained baseline logs record a successful application/test build and
97 passing CTest cases with GCC 14.2 and Qt 6 in Ubuntu 24.04.
LOOT and UNRAR were disabled. Those optional paths, GUI behavior and real-game
compatibility have not been accepted. This handoff did not rerun the baseline.

## Preserved evidence

The full working evidence remains on the VPS at
`/home/christian/Documents/dev/limo-hardening-evidence`.
It includes the original issue ledger, build logs and isolated fixture results.
Generated binaries, source snapshots and detailed reproduction fixtures remain
outside this repository.

| Evidence file | SHA256 |
| --- | --- |
| ledger.md | `d75b6564df6f583f7e57a8b0675851f8b9add175fc3ddc9de2e1fd900c23bfc9` |
| baseline-configure.log | `99e9d900a5291b65499fee0d39f3afafdfaf95e661d565974365a5853856fba6` |
| baseline-build.log | `7447c752b91f7b074d703393d9a853c27070cb77aa60614eea2ba9830e71c3fe` |
| baseline-ctest.log | `c2049b7a037b0facf1d7643f427b13e72ddbef6d5eaafce761387570afd39484` |

## Unfinished work

The original ledger contains confirmed findings and unverified candidates.
They still require scope reconciliation, fixes, regression verification and
independent review. No finding is closed by this checkpoint.

The round-one deployment test and subsequent owner requests were stopped by
the provider. That testing remains paused pending the supported access review.

Backup/profile fixture artifacts are preserved under `round1/state-breaker`;
their presence does not establish an accepted repair or independent review.

## Publication

This topic branch preserves a documentation checkpoint only. The existing CI
push filters target dev and main; releases require a version tag or manual
dispatch. No merge, tag, release or deployment is part of this handoff.
