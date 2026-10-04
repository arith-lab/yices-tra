; Checks that NA adds the definitions it substitutes to the conflict. With the order (d r t n),
; the solver first decides d = false, so the definition t = r^2 holds at decision level 1, above
; the base level. After the decisions r = 3 and t = 9, NA explains the conflict on n by
; substituting t := r^2 into n > t, which gives the cell r > 1. The learned lemma is
; (n > t and n < 1 and t = r^2) => r <= 1. Without the literal t = r^2, the lemma would give
; r <= 1 at the base level, which contradicts r >= 2: a wrong unsat. The formula is satisfiable
; with d = true (r = 3, t = 0, n = 1/2).
(set-logic QF_NRA)
(declare-fun d () Bool)
(declare-fun n () Real)
(declare-fun r () Real)
(declare-fun t () Real)
(set-option :yices-mcsat-var-order (d r t n))
(assert (> n t))
(assert (< n 1))
(assert (>= r 2))
(assert (or d (= t (* r r))))
(check-sat)
