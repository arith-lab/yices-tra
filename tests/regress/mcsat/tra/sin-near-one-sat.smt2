(set-logic QF_TRA)
; sat: x = pi/2 - 2*pi =~ -4.712 gives sin(x) = 1, and -4.712 < 1.
(declare-fun x () Real)
(assert (> (sin x) 0.9999999))
(assert (< x 1))
(check-sat)
