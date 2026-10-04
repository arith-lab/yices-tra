(set-logic QF_TRA)

; Stress test: several nested +, *, /, sin, exp, pi expressions asserted
; simultaneously, in increasing order of nesting depth / function count.

; [1] 3 function applications, value =~ -0.500000
(assert (< (- (+ (sin pi) (exp 0)) 1.5) 0))

; [2] 4 function applications, value =~ 0.718282
(assert (> (- (* (exp 1) (sin (/ pi 2))) 2) 0))

; [3] 5 function applications, value =~ -0.440247
(assert (< (- (- (+ (exp 1) (sin 1)) (sin 0)) 4) 0))

; [4] 7 function applications, value =~ 0.287355
(assert (> (- (+ (* (exp 1) (sin 1)) (- (sin pi) (exp 0))) 1) 0))

; [5] 8 function applications, value =~ 0.562493
(assert (> (- (* (+ (exp 1) (sin 2)) (- (exp 0) (sin (/ pi 4)))) 0.5) 0))

; [6] 9 function applications, value =~ -0.949665
(assert (< (- (+ (sin (exp 1)) (* (exp (sin 1)) 2) (- (exp 0) (sin pi))) 7) 0))

; [7] 10 function applications, value =~ -2.683703
(assert (< (- (* (+ (exp 1) (sin 1) (exp 2)) (- (sin (exp 1)) (exp (sin 2)))) (- 20)) 0))

; [8] 13 function applications, value =~ 0.840676
(assert (> (- (+ (* (exp 1) (sin 1)) (* (exp 2) (sin 2)) (* (exp 3) (sin 3)) (- (sin pi) (exp 0))) 10) 0))

; [9] 14 function applications, value =~ -0.617164
(assert (< (- (+ (sin (* (exp 1) (sin 2))) (exp (/ (sin (exp 2)) 5)) (* (sin 3) (exp (sin 4))) (exp 0)) 3.5) 0))

; [10] 28 function applications, value =~ 4.075723
(assert (> (+ 50 (* (exp (sin (exp (sin 1)))) (sin (exp (sin (exp 2))))) (* (+ (exp 1) (sin 2) (exp 3) (sin 4)) (- (sin (exp 1)) (exp (sin 2)))) (/ (+ (sin pi) (exp 0)) (+ 1 (exp (sin 1))))) 0))

; [11] 7 function applications, value =~ -2.994363
(assert (< (- (+ (sin (* 2 pi)) (exp 1) (* (sin 1) (exp 1))) 8) 0))

; [12] 9 function applications, value =~ 3.722177
(assert (> (- (* (+ (exp 1) (sin 3)) (- (exp 2) (sin 1)) (sin (/ pi 2))) 15) 0))

; [13] 11 function applications, value =~ -7.291130
(assert (< (* (+ (exp 1) (exp 2) (exp 3)) (- (sin 1) (sin 2) (sin 3)) (/ pi (exp 1))) 0))

; [14] 12 function applications, value =~ 11.306335
(assert (> (- (- (* (exp 1) (exp 2) (sin 1)) (+ (sin (exp 3)) (exp (sin 4)) (sin (sin 5)))) 5) 0))

; [15] 13 function applications, value =~ 1.381413
(assert (> (- (+ (sin (exp (sin 2))) (exp (sin (exp 1))) (* (sin 3) (sin 4)) (/ (exp 1) (exp 2))) 1) 0))

; [16] 15 function applications, value =~ -1009.180782
(assert (< (- (- (* (+ (exp 1) (sin 2)) (- (exp 3) (sin 4))) (* (+ (exp 5) (sin 1)) (- (exp 2) (sin 3)))) 3) 0))

; [17] 17 function applications, value =~ 2.443251
(assert (> (- (+ (* (sin 1) (sin 2) (sin 3)) (exp (+ (sin 1) (sin 2))) (/ (sin (exp 1)) (exp (sin 2))) (sin (exp (exp 0)))) 4) 0))

; [18] 17 function applications, value =~ -0.155557
(assert (< (- (+ (sin (* (exp 1) (sin 2) (exp 3))) (exp (/ (sin (exp 4)) 5)) (* (sin 5) (exp (sin 6))) (exp (sin (exp 0)))) 2) 0))

; [19] 20 function applications, value =~ -54.566292
(assert (< (- (+ (* (exp 1) (sin 2) (exp 3) (sin 4)) (- (sin (exp 1)) (exp (sin 2)) (sin (exp 3))) (/ (+ (sin 1) (exp 1)) (+ 1 (exp (sin 2))))) 15) 0))

; [20] 38 function applications, value =~ -55.247067
(assert (< (- (+ 50 (* (exp (sin (exp (sin 1)))) (sin (exp (sin (exp 2))))) (* (+ (exp 1) (sin 2) (exp 3) (sin 4)) (- (sin (exp 1)) (exp (sin 2)))) (/ (+ (sin pi) (exp 0)) (+ 1 (exp (sin 1)))) (* (sin (exp (sin 3))) (exp (sin (exp 4))) (- (exp 1) (sin 1)))) 60) 0))

; overall expected result: sat (all 20 ground facts above are true)
(check-sat)
