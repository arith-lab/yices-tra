(set-logic QF_TRA)
; sum of squares of transcendentals
; value =~ 4.87002733223
(assert (> (+ (* (sin 1) (sin 1)) (* (sin 2) (sin 2)) (* (exp (/ 1 2)) (exp (/ 1 2))) (* (/ pi 4) (/ pi 4))) (/ 3712621 763104)))
(check-sat)
