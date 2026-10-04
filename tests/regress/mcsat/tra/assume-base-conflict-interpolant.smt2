; The assertions are unsatisfiable at the base level (y = pi and y = x + pi give x = 0, against
; x > pi). With the option, NA finds this in its learn step, as a conflict without a cause; with
; assumptions MCSAT asks TRA for the conflict, which is then empty (before: assertion in debug
; builds, unknown in release builds).
(set-option :produce-unsat-model-interpolants true)
(set-logic QF_TRA)
(declare-fun x () Real)
(declare-fun y () Real)
(assert (> x pi))
(assert (= y pi))
(assert (= y (+ x pi)))
(check-sat-assuming-model (y) (1.0))
(get-unsat-model-interpolant)
