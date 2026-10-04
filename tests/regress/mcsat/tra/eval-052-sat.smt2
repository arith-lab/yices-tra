(set-logic QF_TRA)
; polynomial in exp(1), degree 3
; value =~ 5.07321411177
(assert (> (+ (* (exp 1) (exp 1) (exp 1)) (* (- 0 3) (exp 1) (exp 1)) (* 3 (exp 1)) (- 0 1)) (/ 2896478 571507)))
(check-sat)
