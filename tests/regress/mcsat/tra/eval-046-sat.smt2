(set-logic QF_TRA)
; nested divisions
; value =~ 8.38535011303
(assert (> (/ (/ (exp 2) (sin 1)) (/ pi 3)) (/ 2360436 281777)))
(check-sat)
