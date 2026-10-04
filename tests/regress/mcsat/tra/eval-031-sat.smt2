(set-logic QF_TRA)
; sum of nested sin(exp)
; value =~ 0.632647906404
(assert (> (+ (sin (exp 1)) (sin (exp 2)) (sin (exp 3)) (sin (exp 4)) (sin (exp 5))) (/ 609379 964184)))
(check-sat)
