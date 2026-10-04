(set-logic QF_TRA)
; the ite is sin |x|, and sin |x| > 0.9 holds for |x| in (asin 0.9, pi - asin 0.9), e.g. x = 1.5
(declare-fun x () Real)
(assert (< x 2))
(assert (> x (- 2)))
(assert (> (ite (>= x 0) (sin x) (- (sin x))) 0.9))
(check-sat)
