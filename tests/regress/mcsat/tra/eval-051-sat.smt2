(set-logic QF_TRA)
; deep alternating exp/sin nest
; value =~ 0.415656197543
(assert (> (sin (exp (sin (exp (sin (exp 1)))))) (/ 344038 828527)))
(check-sat)
