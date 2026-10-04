# Stress tests for the MCSAT solver of YicesTRA

These instances come from `tests/regress/mcsat/bv` of Yices 2. Yices 2 solves them in less than
a second, but YicesTRA 1.0.0 times out on them: the preprocessor of YicesTRA splits the assertions
into conjuncts, which changes the search for every MCSAT logic. They are kept out of
`tests/regress/mcsat`, so that the regression suite passes. The expected answer of `file.smt2` is
in `file.smt2.gold`.

To run them:

```
tests/regress/check.sh tests/stress/mcsat build/<platform>-release/bin
```

When a new version of YicesTRA solves one of them, move it back, with its `.gold` and `.options`
files, to `tests/regress/mcsat/bv`.
