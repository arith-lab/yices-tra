(set-logic QF_TRA)
; sum with one huge summand
; value =~ 485165202.254
(assert (> (+ (exp 20) (sin 1) (exp 1) pi (/ 1 7)) (/ 405391714470482 836411)))
(check-sat)
