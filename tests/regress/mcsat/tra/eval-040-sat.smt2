(set-logic QF_TRA)
; sin of exp(exp 5)
; value =~ 0.957538707047
(assert (> (sin (exp (exp 5))) (/ 613753 641611)))
(check-sat)
