; Contradictory assumptions (a and (not a)) must give unsat (the literal shortcut mapped a twice).
(set-logic QF_NRA)
(declare-fun x () Real)
(declare-fun a () Bool)
(assert (> (* x x) 1))
(check-sat-assuming (a (not a)))
(check-sat-assuming (a))
(check-sat)
