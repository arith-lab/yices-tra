(set-logic QF_TRA)
; sin 4 < 0, so the ite is -pi, and -pi > 3 is false
(assert (> (ite (>= (sin 4) 0) pi (- pi)) 3))
(check-sat)
