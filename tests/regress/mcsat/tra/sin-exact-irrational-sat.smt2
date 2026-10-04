; No exact value for sin(n pi / 6) with n mod 6 in {2, 4} (±√3/2) or for a denominator other
; than 1, 2, 3, 6 (here pi/4): a wrong lemma would make these bounds unsat.
(set-logic QF_TRA)
(assert (and (> (sin (/ pi 3)) 0.86) (< (sin (/ pi 3)) 0.87)
             (> (sin (* (/ 2 3) pi)) 0.86)
             (< (sin (* (/ 4 3) pi)) (- 0.86))
             (< (sin (* (/ 5 3) pi)) (- 0.86))
             (> (sin (/ pi 4)) 0.7) (< (sin (/ pi 4)) 0.71)
             (> (sin (/ pi 6)) 0.4)))
(check-sat)
