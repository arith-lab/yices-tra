; Every rational value sin(n pi / 6) (n mod 12 in {0, 1, 3, 5, 7, 9, 11}; n = 6 is sin pi, an
; anchor) with a zero margin, for n = 13, 5, 7, 3, 9, 12, -1, 18: each disjunct is false.
(set-logic QF_TRA)
(assert (or (< (sin (* (/ 13 6) pi)) 0.5)
            (< (sin (* (/ 5 6) pi)) 0.5)
            (> (sin (* (/ 7 6) pi)) (- 0.5))
            (< (sin (/ pi 2)) 1)
            (> (sin (* (/ 3 2) pi)) (- 1))
            (> (sin (* 2 pi)) 0)
            (> (sin (* (- (/ 1 6)) pi)) (- 0.5))
            (< (sin (* 3 pi)) 0)))
(check-sat)
