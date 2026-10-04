(set-logic QF_TRA)
(assert (< (+ (* (sin (/ 1 0)) 2) (exp (- 10))) 0))
(check-sat)
