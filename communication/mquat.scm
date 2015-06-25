
(define ms mquat-spec)

;; Properties for Cubieboards
(define load (:RealProperty ms "Load" "%" 'runtime 'decreasing agg-sum))
(define cpu-freq (:RealProperty ms "CPU.Frequency" "Mhz" 'runtime 'increasing 'agg))
(define mem-free (:RealProperty ms "Memory.Free" "MB" 'runtime 'increasing 'sum))
;; Resource-Types
(define Switch (:ResourceType ms "Switch" #f (list)))
(define Cubieboard (:ResourceType ms "Cubieboard" #t (list cpu-freq load mem-free)))
;; Properties for Sample-Component
(define energy (:RealProperty ms pn-energy "J" 'runtime 'decreasing 'sum))
(define response-time (:RealProperty ms "Response-time" "ms" 'runtime 'decreasing 'sum))
(define precision (:RealProperty ms "Precision" #f 'runtime 'increasing 'agg))
(define pal (list (cons 'energy energy) (cons 'response-time response-time) (cons 'precision precision)))
;; MetaParameter-Names and -Nodes
;(define workers "workers")
;(define particles "particles")
;(define mp-workers (:MetaParameter ms workers #f))
;(define mp-particles (:MetaParameter ms particles #f))
;; ReqClause construction
(define sample-response-time (:PropertyRef ms response-time))
(define sample-energy (:PropertyRef ms energy))
(define (make-energy-f workers particles)
  (let ([val (inexact (case workers
               [(1) (+ #e158.418575 (* #e-0.603838 particles) (* #e0.001126 particles particles))]
               [(3) (+ #e146.0164 (* #e0.2118 particles))]
               [(5) (+ #e783.600532 (* #e-2.254366 particles) (* #e0.002543 particles particles))]
               [else (error 'energy-f "Unknown number of workers" workers)]))])
    (lambda (args type) val)))
(define (make-rt-f workers particles)
  (let ([val (inexact (case workers
               [(1) (+ #e137.3145 (* #e0.8858 particles))]
               [(3) (+ #e242.506 (* #e0.443 particles))]
               [(5) (+ #e255.5215 (* #e0.2988 particles))]
               [else (error 'rt-f "Unknown number of workers" workers)]))])
    (lambda (args type) val)))
(define (make-precision-f workers particles)
  (let ([val (inexact (case workers
                        [(1) (+ #e4072.329 (* #e-2.001 particles))]
                        [(3) (+ #e4295.985 (* #e-5.227 particles))]
                        [(5) (max (+ #e3626.028 (* #e-5.672 particles)) 10)]
                        [else (error 'precision-f "Unknown number of workers" workers)]))])
    (lambda (args type) val)))
;; Software entities
(define (make-mode workers particles)
  (:Mode ms (string-append "KLD-" (number->string workers) "-" (number->string particles))
         (list (:ProvClause ms sample-energy comp-max-eq (make-energy-f workers particles))
               (:ProvClause ms precision comp-min-eq (make-precision-f workers particles))
               (:ProvClause ms sample-response-time comp-max-eq (make-rt-f workers particles))
               )))
(define modes (map make-mode (list 1 1 1 3 3 3 5 5 5) (list 300 500 700 300 500 700 300 500 700)))
(define kld (:Impl ms "KLD" modes (list) #f #f))
(define Sample (:Comp ms "Sampling" (list kld) #f (list precision sample-response-time sample-energy)))

(define ast
  (:Root ms
         (:HWRoot ms (list Cubieboard) (list) (list))
         (:SWRoot ms (list Sample) (list energy response-time))
         (:Request ms (list) Sample (list) #f)))

(define (update-status worker-id status . valid-former-stati)
  (let ([pe (=search-pe ast (r worker-id))])
    (when (and (not (null? valid-former-stati)) (not (memq (->status pe) valid-former-stati)))
      (rewrite-terminal 'status pe error-state)
      (warn "worker" worker-id "now in error-state"))
    (rewrite-terminal 'status pe status)))

(define (search-parent id) (if (eq? 0 id) (->HWRoot ast) (=search-pe ast (r id))))
(define ° "racr-mquat>")
(define (r id) (string-append "r-" (number->string id)))

(define (update-request-constraints property property-value comparator)
      (let ([clause? (ast-find-child (lambda (i n) (eq? (=real property) (=real (->return-type n))))
                                     (->Constraints (<=request ast)))])
        (if (and property-value (> 0 property-value))
            (if clause? (rewrite-terminal 'value clause? (lambda _ property-value)) ; rewrite existing clause
                ; add new clause
                (rewrite-add (->Constraints (<=request ast)) (:ReqClause ms property comparator (lambda _ property-value))))
            (when clause? (rewrite-delete clause?))))) ; delete existing clause

(define (update-request-objective objective)
  (let ([new-objective (assq objective pal)]
        [old-objective (->objective (<=request ast))])
    (if new-objective
        (when (not (eq? old-objective (cdr new-objective)))
          (rewrite-terminal 'objective (<=request ast) (->name (cdr new-objective))))
        (warn "Unknown objective" objective))))

(define (event-work-complete id time work-id)
  ; TODO do something useful
  (display "event-work-complete\n") (flush-output-port (current-output-port))
  (info ° "check if deadline was met" id time work-id))

(define (event-work-request time work-id load-size deadline)
  ; TODO do something useful
  (display "event-work-request\n") (flush-output-port (current-output-port))
  (info ° "call \"schedule\"-attribute to find worker and insert request into workers queue" time work-id load-size deadline))

(define (event-request time work-id objective max-energy max-response-time min-precision)
  ; New event for racr-mquat ;
  (display "event-work-request\n") (flush-output-port (current-output-port))
  (info ° "event-work" time work-id objective max-energy max-response-time min-precision)
  (update-request-constraints energy max-energy comp-max-eq)
  (update-request-constraints response-time max-response-time comp-max-eq)
  (update-request-constraints precision min-precision comp-min-eq)
  (update-request-objective objective)
  (save-ilp (string-append "ilps/" (number->string work-id) ".lp") ast))

(define (event-worker-off worker-id time)
  (info ° "event-worker-off" worker-id time) (flush-output-port (current-output-port))
  (update-status worker-id offline))

(define (event-worker-offline worker-id time)
  (info ° "event-worker-offline" worker-id time)
  (update-status worker-id offline halting))

(define (event-worker-online worker-id time)
  (info ° "event-worker-online" worker-id time) (flush-output-port (current-output-port))
  (update-status worker-id online))

(define (event-adapt time)
  ; TODO do something useful
  (info ° "call adapt" time) (flush-output-port (current-output-port)))

(define (add-worker-to-ast id parent-id device-type time)
  (info ° "add-worker-to-ast" id parent-id device-type time) (flush-output-port (current-output-port))
  (let ([new-pe (:Resource ms (r id) Cubieboard offline (list) (list))]
        [parent-pe (search-parent parent-id)])
    (rewrite-add (->SubResources parent-pe) new-pe)))

(define (add-switch-to-ast id parent-id time)
  (info ° "add-switch-to-ast" id parent-id time) (flush-output-port (current-output-port))
  (let ([new-pe (:Resource ms (r id) Switch offline (list) (list))]
        [parent-pe (search-parent parent-id)])
    (rewrite-add (->SubResources parent-pe) new-pe)))

(define (set-backup-workers number)
  ; TODO do something useful
  (info ° "set-backup-workers" number) (flush-output-port (current-output-port)))

(define (display-ast)
  (info ° "displays-ast\n") (flush-output-port (current-output-port))
  (let ([print (lambda (name) (cons name (lambda (v) v)))])
    (define printer (list)); (print 'eval)))
    (print-ast ast printer (current-output-port))))

(define (display-request)
  (info ° "displays-request\n") (flush-output-port (current-output-port))
  (print-ast (<=request ast) (list) (current-output-port)))
