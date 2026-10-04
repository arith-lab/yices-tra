; An equality between terms of an uninterpreted sort in the guard of an if-then-else is not
; supported by the analysis: unsupported theory (before: assertion in debug builds, timeout in
; release builds).
(set-logic QF_TRA)
(declare-sort U 0)
(declare-fun x () Real)
(declare-fun u () U)
(declare-fun v () U)
(assert (> (ite (= u v) (sin x) 2.0) 1.5))
(check-sat)
