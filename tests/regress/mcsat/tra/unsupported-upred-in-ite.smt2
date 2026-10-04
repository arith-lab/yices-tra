; An uninterpreted predicate in the guard of an if-then-else is not supported by the analysis:
; unsupported theory (before: assertion in debug builds, timeout in release builds).
(set-logic QF_TRA)
(declare-fun x () Real)
(declare-fun p (Real) Bool)
(assert (> (ite (p x) (sin x) 2.0) 1.5))
(check-sat)
