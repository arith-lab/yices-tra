; to_int in an atom that gets its value only during the search: unsupported theory, raised at
; assertion time (before: crash, since the analysis raised the exception during the search).
(set-logic QF_TRA)
(declare-fun x () Real)
(declare-fun b () Bool)
(assert (or (= (to_int x) 3) b))
(check-sat)
