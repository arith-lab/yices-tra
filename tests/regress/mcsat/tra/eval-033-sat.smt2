(set-logic QF_TRA)
; cube of exp(1/2), factor in (1,2)
; value =~ 4.48168907034
(assert (> (* (exp (/ 1 2)) (exp (/ 1 2)) (exp (/ 1 2))) (/ 4205911 939405)))
(check-sat)
