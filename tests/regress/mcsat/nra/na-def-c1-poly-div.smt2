; Divisions become auxvars tied by equations (purification, division, alias).
; NA solves it by substituting the definitions in conflict explanations (off with the environment
; variable YICES_NA_NO_DEF_SUBST); deciding the auxvars after their definitions (off
; with YICES_NA_NO_DEF_ORDER) makes it faster.
(set-logic QF_NRA)
(declare-fun n () Real)
(declare-fun r () Real)
(declare-fun A () Real)
(declare-fun B () Real)
(declare-fun C () Real)
(assert (>= n 2))
(assert (<= (* r r) (* n (- n 1))))
(assert (= A (- (/ (* (- r 1) (- r 1)) (* 2 (- n 1))))))
(assert (= B (- (/ (* (+ r 1) (+ r 1)) (* 2 (- n 1))))))
(assert (= C (- (/ (* r r) (* 2 n)))))
(assert (< (- C (/ (+ A B) 2) (/ (* (- A B) (- A B)) 8)) 0))
(check-sat)
