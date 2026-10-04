(set-logic QF_TRA)
; exp 1 < 3 and sin 1 > 0, so the nested ite is 1, and 1 < 2
(assert (< (ite (< (exp 1) 3) (ite (> (sin 1) 0) 1 2) 3) 2))
(check-sat)
