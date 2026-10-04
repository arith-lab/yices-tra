(set-logic QF_TRA)
; product of three sums
; value =~ 64.8883211697
(assert (> (* (+ (exp 1) 1) (+ (sin 1) 2) (+ pi 3)) (/ 48057565 741361)))
(check-sat)
