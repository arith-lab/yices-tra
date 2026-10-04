(set-logic QF_TRA)
(declare-fun x () Real)
; unsat: for x >= 1, exp(exp(x-1)) + exp(exp(x-2)) < exp(exp x)
; (the ratio peaks at 0.275 for x = 1 and decays from there).
; The gold file used to say sat, back when this test still declared exp
; itself and so got the uninterpreted reading, under which it is trivial.
(assert (>= x 1))
(assert (> (+ (exp (exp (- x 1))) (exp (exp (- x 2)))) (exp (exp x))))
(check-sat)
