; div in an atom that gets its value only during the search: unsupported theory, raised at
; assertion time (before: crash, since the analysis raised the exception during the search).
(set-logic QF_TRA)
(declare-fun x () Real)
(declare-fun n () Int)
(declare-fun b () Bool)
(assert (or (> (div n 2) 1) b))
(assert (> (sin x) 0.5))
(check-sat)
