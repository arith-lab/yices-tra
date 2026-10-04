; The exact value of sin(pi/6) holds in the scope of the purification of pi/6, and again after
; the pop, where pi/6 gets a new purification variable.
(set-logic QF_TRA)
(push 1)
(assert (> (sin (/ pi 6)) 0.5))
(check-sat)
(pop 1)
(assert (> (sin (/ pi 6)) 0.4))
(check-sat)
(assert (< (sin (/ pi 6)) 0.5))
(check-sat)
