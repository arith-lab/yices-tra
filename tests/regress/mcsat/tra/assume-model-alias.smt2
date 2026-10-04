; The preprocessor solves s := (sin x) and e := (exp x). A model assumption on s or e makes
; mcsat_solve assert s = (sin x) (e = (exp x)) without preprocessing; the TRA plugin must still
; check it (before the fix: sat, with (sin x) = 0.7 at x = 0.5 and (exp x) = 7 at x = 2).
(set-logic QF_TRA)
(declare-fun x () Real)
(declare-fun s () Real)
(declare-fun e () Real)
(assert (= s (sin x)))
(assert (= e (exp x)))
(check-sat-assuming-model (x s) (0.5 0.7))
(check-sat-assuming-model (x e) (2.0 7.0))
(check-sat-assuming-model (x s e) (0.0 0.0 1.0))
