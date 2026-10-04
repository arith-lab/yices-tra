(set-logic QF_TRA)
; sin 3 > 0, so the ite is pi, and pi > 3
(assert (> (ite (>= (sin 3) 0) pi (- pi)) 3))
(check-sat)
