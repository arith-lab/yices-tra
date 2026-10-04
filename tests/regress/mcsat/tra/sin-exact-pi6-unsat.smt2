; sin(pi/6) = 1/2 exactly: the argument stands for q * pi with sin(q pi) rational (Niven), and the
; sin plugin emits the exact value (before: timeout, since the margin is zero).
(set-logic QF_TRA)
(assert (> (sin (/ pi 6)) 0.5))
(check-sat)
