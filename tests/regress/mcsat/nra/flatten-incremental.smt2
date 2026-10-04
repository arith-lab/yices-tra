; MCSAT asserts the conjuncts of an assertion separately, so that the
; equalities (= y ...) and (= z ...) are solved by the preprocessor.
(set-logic QF_NRA)
(declare-fun x () Real)
(declare-fun y () Real)
(declare-fun z () Real)
(push 1)
; unsat: y = x^2 >= 0, hence z = y + 1 >= 1
(assert (and (= y (* x x)) (and (= z (+ y 1)) (< z 1))))
(check-sat)
(pop 1)
; sat: x = -2, y = 4
(assert (and (= y (* x x)) (not (or (< y 4) (> x 0)))))
(check-sat)
(assert (< y 4))
(check-sat)
