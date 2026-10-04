(set-logic QF_TRA)
; sin(exp(exp 4)) times pi
; value =~ -0.463808372295
(assert (< (* (sin (exp (exp 4))) pi) (- 0 (/ 114833 247835))))
(check-sat)
