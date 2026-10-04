[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)

# YicesTRA

YicesTRA is a tool to decide the satisfiability of quantifier-free formulas of non-linear
real arithmetic extended with transcendental functions. The current version supports the
sine function `sin`, the exponential function `exp`, and the constant `pi`. YicesTRA is an
extension of the [Yices 2](https://github.com/SRI-CSL/yices2) SMT solver, and implements an
extension of the procedure of the paper
[MCSAT Modulo Transcendental Arithmetics](https://arxiv.org/abs/2606.00697v1) (FMCAD 2026).

By default, YicesTRA looks for an exact answer, and only reports `sat` or `unsat`. When a
tolerance δ is fixed with the option `--mcsat-delta`, it may also report `delta-sat`, and
it stops as soon as an assignment satisfies the formula up to δ.

## Installation

YicesTRA requires:

- [gperf](https://www.gnu.org/software/gperf/) and [GMP](https://gmplib.org/), as for Yices 2;
- [libpoly](https://github.com/SRI-CSL/libpoly) and [CUDD](https://github.com/ivmai/cudd)
  (configured with `--enable-shared`), as for the MCSAT engine of Yices 2;
- [FLINT](https://flintlib.org/), version 3 or later (it includes the Arb module), and
  [MPFR](https://www.mpfr.org/).

Once the dependencies are installed, build the tool with:

```
git clone https://github.com/arith-lab/yices-tra.git
cd yices-tra
autoconf
./configure
make
```

The binary is placed in `build/<platform>-release/bin/ytra`, for instance
`build/x86_64-pc-linux-gnu-release/bin/ytra` on Linux.

### Troubleshooting

- If `./configure` does not find a library, pass its location with `CPPFLAGS` and
  `LDFLAGS`, for instance `./configure CPPFLAGS=-I/opt/flint/include LDFLAGS=-L/opt/flint/lib`.
  Run `./configure --help` for the other options.
- When building CUDD, `make` may fail with `[Makefile:983: aclocal.m4] Error 127`. In this
  case, run `autoreconf -fi`, and then `./configure --enable-shared` again.
- `./configure` requires the headers `flint/arb.h` and `flint/fmpz.h`. Before version 3,
  Arb was a separate library, which installed `arb.h` directly in `include/`. If
  `./configure` reports that these headers are missing, install FLINT 3 or later.
- If `ytra` stops with "error while loading shared libraries", add the directories of FLINT,
  libpoly or CUDD to `LD_LIBRARY_PATH` (for example `export LD_LIBRARY_PATH=/opt/flint/lib`),
  or register them with `ldconfig`.

## Running the tool

YicesTRA reads the [SMT-LIB 2](https://smt-lib.org/) format. For instance, the following
formula asks for a real number x in [0, π/2] such that sin(x)² > 1/2 and exp(x) < 3:

```smt2
(set-logic QF_TRA)
(declare-fun x () Real)
(assert (<= 0 x))
(assert (<= x (* (/ 1 2) pi)))
(assert (> (* (sin x) (sin x)) (/ 1 2)))
(assert (< (exp x) 3))
(check-sat)
(get-model)
```

```
$ ytra intro.smt2
sat
((define-fun x () Real 1.0))
```

The model x = 1 is indeed a solution, since sin(1)² ≈ 0.708 > 1/2 and exp(1) ≈ 2.718 < 3.

### Input format

- The file must start with `(set-logic QF_TRA)`. This logic predefines the functions `sin`
  and `exp`, of type Real → Real, and the constant `pi`. They must not be declared.
- The other constructs are those of SMT-LIB 2 for real arithmetic: `declare-fun` and
  `declare-const` for variables of sort Real, Int and Bool, the operations `+`, `-`, `*` and `/`,
  the predicates `<`, `<=`, `>`, `>=` and `=`, the Boolean connectives, `ite` and `let`,
  and the commands `assert`, `check-sat` and `get-model`.
- While Int variables are accepted, integer operations `div`, `mod`, `to_int`, `is_int` and `divisible` are not supported yet.
- `get-value` answers `unsupported`: the values of `pi`, `sin` and `exp` in a model are
  approximations, so the values of terms that contain them would be too.
- Other transcendental functions are not built in. Trigonometric functions can be
  expressed with `sin` and `pi`: for instance, `(cos x)` is `(sin (+ x (* (/ 1 2) pi)))`.

### Command line

```
ytra [options] file.smt2
```

| Option | Meaning |
|---|---|
| `--mcsat-delta=D` | Fixes the tolerance δ = 2<sup>−D</sup>, for a positive integer D, and enables the answer `delta-sat`. |
| `--dump-models` | Prints a model after every `sat` or `delta-sat` answer, even without `(get-model)`. |
| `--yices-model-format` | Prints models in the format of Yices instead of SMT-LIB 2. |
| `--timeout=S` | Stops the solver after S seconds. |
| `--stats` | Prints statistics on the search at the end of the run. |
| `--mcsat-help` | Lists further options of the MCSAT engine. |

A `delta-sat` answer means that the model satisfies the δ-weakening of the formula, in
which every atom involving a transcendental function is relaxed by δ: for instance, the
δ-weakening of sin(x) = 1/2 is |sin(x) − 1/2| ≤ δ. This answer is needed when every
solution is transcendental, as YicesTRA only computes algebraic values. For instance, the
only solution of sin(x) = 1/2 in [0, 2] is π/6: without `--mcsat-delta`, the solver refines
its approximation of π/6 until the time limit is reached.

In the format of Yices, models also list values for `pi`, `sin` and `exp`. These values are
artifacts of the internal computations: ignore them when checking a solution.

## License

YicesTRA is a modified version of [Yices 2](https://github.com/SRI-CSL/yices2), copyright SRI
International, changed by IMDEA Software Institute in 2026. It is free software, distributed
under the GNU General Public License, version 3 or (at your option) any later version: see
`LICENSE.txt`. The libraries that YicesTRA uses, and their licenses, are listed in `etc/NOTICES`.

## Documentation

We have not yet modified the man pages in `doc/`, `FAQ.md` and `etc/README.*`: they describe
Yices 2, and do not cover `QF_TRA`. For now, this README is the only documentation of YicesTRA.