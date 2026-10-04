(set-logic QF_TRA)
; exp(1)^2 * sin(1)^3
; value =~ 4.40257132022
(assert (> (* (exp 1) (exp 1) (sin 1) (sin 1) (sin 1)) (/ 51397 11686)))
(check-sat)
