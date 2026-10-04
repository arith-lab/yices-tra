; As check-sat-assuming-model-pi, with pi in an assertion: NA refutes pi = 3.0 with the bounds of
; pi, and the TRA plugin refutes the values within the bounds (before the fix: sat for those).
(set-logic QF_TRA)
(declare-fun x () Real)
(assert (> x pi))
(check-sat-assuming-model (pi) (3.0))
(check-sat-assuming-model (pi) (3.141592653589793))
(check-sat-assuming-model (x pi) (4.0 3.14159265))
(check-sat-assuming-model (x) (4.0))
