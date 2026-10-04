(set-logic QF_TRA)
(declare-fun x () Real)
(declare-fun z () Real)
; unsat: x * x = z * z with x, z > 0 gives x = z, hence (exp x) = (exp z). The trail values
; of x and z are equal: congruence (UF plugin) decides it before the tie case of the
; monotonicity conflict, which is a fallback.
(assert (> x 0))
(assert (> z 0))
(assert (= (* x x) (* z z)))
(assert (> (exp x) (exp z)))
(check-sat)
