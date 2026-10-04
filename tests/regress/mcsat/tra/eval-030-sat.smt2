(set-logic QF_TRA)
; alternating signs, near-cancelling sum
; value =~ 0.001
(assert (> (+ (exp 1) (- 0 (exp 1)) (sin 1) (- 0 (sin 1)) pi (- 0 pi) (/ 1 1000)) (/ 999 1000000)))
(check-sat)
