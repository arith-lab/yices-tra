(set-logic QF_TRA)
; sum of 4 products of exp/sin
; value =~ -29.4793400672
(assert (< (+ (* (exp 1) (sin 1)) (* (exp 2) (sin 2)) (* (exp 3) (sin 3)) (* (exp 4) (sin 4))) (- 0 (/ 14220307 482865))))
(check-sat)
