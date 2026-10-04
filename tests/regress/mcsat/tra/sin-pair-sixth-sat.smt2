(set-logic QF_TRA)
(declare-fun x () Real)
; sat: sin x + sin(x + pi/6) > 1.8 on (0.94, 1.68). A control: the pair relations are valid, so the answer
; stays sat. A pair with n = +-1 (mod 6) has no rational relation and must be skipped.
(assert (> x 0.0))
(assert (< x 2.0))
(assert (> (+ (sin x) (sin (+ x (/ pi 6)))) 1.8))
(check-sat)
