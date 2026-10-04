; The bounds on (exp 1) were first handed over while the solver was inconsistent, so MCSAT dropped
; them; after the pop, (exp 1) had no variable (debug: assert in tra_exp_let_na_decide).
(set-logic QF_TRA)
(declare-fun x () Real)
(declare-fun y () Real)
(push 1)
(assert (and (> (exp x) 5) (< x 0) (> x 1)))
(check-sat)
(pop 1)
(assert (> (exp y) 3))
(check-sat)
(assert (= y 1))
(check-sat)
