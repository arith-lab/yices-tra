(set-logic QF_TRA)
; polynomial in pi, degree 4, rational coefficients
; value =~ 38.2137168288
(assert (> (+ (* (/ 1 2) pi pi pi pi) (* (- 0 (/ 3 5)) pi pi pi) (* (/ 7 8) pi pi) (* (- 0 (/ 2 3)) pi) (/ 11 7)) (/ 4152464 108773)))
(check-sat)
