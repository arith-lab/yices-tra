# Stress tests for YicesTRA

YicesTRA 1.0.0 does not solve these instances within the time limit of the regression
harness (60 seconds of CPU time): it either times out or answers `unknown`. They are kept
out of `tests/regress/mcsat/tra`, so that the regression suite passes. The expected answer
of `file.smt2` is in `file.smt2.gold`, when it is known (`issue-002.smt2` has none).

To run them:

```
tests/regress/check.sh tests/stress/tra build/<platform>-release/bin
```

When a new version of YicesTRA solves one of them, move it back, with its `.gold` file,
to `tests/regress/mcsat/tra`, unless it is too slow for the regression suite (several seconds):
`louis-exp-problem` (about 4 s with a devel build) and `trigpoly-356-4a-sin2x` (about 19 s) are
solved but stay here.
