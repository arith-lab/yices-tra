; Unsatisfiable at the base level without transcendental terms and without options: NA's learn
; step finds the conflict (before: assertion in debug builds, unknown in release builds).
(set-logic QF_TRA)
(declare-fun x () Real)
(declare-fun y () Real)
(assert (<= 0 x))
(assert (<= x 1))
(assert (<= 2 y))
(assert (<= y 3))
(assert (> x y))
(check-sat-assuming-model (x) (0.5))
