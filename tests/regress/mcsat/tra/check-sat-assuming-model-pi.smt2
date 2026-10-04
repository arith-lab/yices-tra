; An assumption on pi must be checked against pi. Here pi occurs in no assertion, so no bound on pi
; is in the trail (before the fix: sat for pi = 3.0 and for pi = 3.141592653589793).
(set-logic QF_TRA)
(declare-fun x () Real)
(assert (> x 0.0))
(check-sat-assuming-model (pi) (3.0))
(check-sat-assuming-model (pi) (3.141592653589793))
(check-sat)
