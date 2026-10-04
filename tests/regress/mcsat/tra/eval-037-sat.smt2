(set-logic QF_TRA)
; 8th power of sin(1), factor < 1
; value =~ 0.251369836996
(assert (> (* (sin 1) (sin 1) (sin 1) (sin 1) (sin 1) (sin 1) (sin 1) (sin 1)) (/ 56972 226873)))
(check-sat)
