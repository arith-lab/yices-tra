(set-logic QF_TRA)
; division by a value in (1,2)
; value =~ 12.1824939607
(assert (> (/ (exp 3) (exp (/ 1 2))) (/ 7343213 603371)))
(check-sat)
