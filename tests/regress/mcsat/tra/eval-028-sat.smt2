(set-logic QF_TRA)
; sum of 6 summands with rational coefficients
; value =~ 3.35599564634
(assert (> (+ (* (/ 1 3) (exp 1)) (* (/ 2 7) (sin 1)) (* (/ 5 11) pi) (* (/ 1 13) (exp 2)) (* (/ 3 17) (sin 2)) (/ 1 19)) (/ 2100821 626617)))
(check-sat)
