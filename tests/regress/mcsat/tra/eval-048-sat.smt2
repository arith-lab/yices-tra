(set-logic QF_TRA)
; sum of quotients
; value =~ 1.88029234634
(assert (> (+ (/ (exp 1) 3) (/ (sin 1) 7) (/ pi 11) (/ (exp 2) 13)) (/ 1788716 952249)))
(check-sat)
