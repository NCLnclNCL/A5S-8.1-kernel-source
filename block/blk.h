#ifndef BLK_INTERNAL_H
#define BLK_INTERNAL_H

#include <linux/idr.h>
#include <linux/blk-mq.h>
#include "blk-mq.h"

/* Amount of time in which a process may batch requests */
#define BLK_BATCH_TIME	(HZ/50UL)

/* Number of requests a "batching" process may submit */
#define BLK_BATCH_REQ	32

/* Max future timer expiry for timeouts */
#define BLK_MAX_TIMEOUT		(5 * HZ)

struct blk_flush_queue {
	unsigned int		flush_queue_delayed:1;
	unsigned int		flush_pending_idx:1;
	unsigned int		flush_running_idx:1;
	unsigned long		flush_pending_since;
	struct list_head	flush_queue[2];
	struct list_head	flush_data_in_flight;
	struct request		*flush_rq;

	/*
	 * flush_rq shares tag with this rq, both can't be active
	 * at the same time
	 */
	struct request		*orig_rq;
	spinlock_t		mq_flush_lock;
};

extern struct kmem_cache *blk_requestq_cachep;
extern struct kmem_cache *request_cachep;
extern struct kobj_type blk_queue_ktype;
extern struct ida blk_queue_ida;

static inline struct blk_flush_queue *blk_get_flush_queue(
		struct request_queue *q, struct blk_mq_ctx *ctx)
{
	if (q->mq_ops)
		return blk_mq_map_queue(q, ctx->cpu)->fq;
	return q->fq;
}

static inline void __blk_get_queue(struct request_queue *q)
{
	kobject_get(&q->kobj);
}

struct blk_flush_queue *blk_alloc_flush_queue(struct request_queue *q,
		int node, int cmd_size);
void blk_free_flush_queue(struct blk_flush_queue *q);

int blk_init_rl(struct request_list *rl, struct request_queue *q,
		gfp_t gfp_mask);
void blk_exit_rl(struct request_list *rl);
void init_request_from_bio(struct request *req, struct bio *bio);
void blk_rq_bio_prep(struct request_queue *q, struct request *rq,
			struct bio *bio);
void blk_queue_bypass_start(struct request_queue *q);
void blk_queue_bypass_end(struct request_queue *q);
void blk_dequeue_request(struct request *rq);
void __blk_queue_free_tags(struct request_queue *q);
bool __blk_end_bidi_request(struct request *rq, int error,
			    unsigned int nr_bytes, unsigned int bidi_bytes);
void blk_freeze_queue(struct request_queue *q);

static inline void blk_queue_enter_live(struct request_queue *q)
{
	/*
	 * Given that running in generic_make_request() context
	 * guarantees that a live reference against q_usage_counter has
	 * been established, further references under that same context
	 * need not check that the queue has been frozen (marked dead).
	 */
	percpu_ref_get(&q->q_usage_counter);
}

#ifdef CONFIG_BLK_DEV_INTEGRITY
void blk_flush_integrity(void);
#else
static inline void blk_flush_integrity(void)
{
}
#endif

void blk_timeout_work(struct work_struct *work);
unsigned long blk_rq_timeout(unsigned long timeout);
void blk_add_timer(struct request *req);
void blk_delete_timer(struct request *);


bool bio_attempt_front_merge(struct request_queue *q, struct request *req,
			     struct bio *bio);
bool bio_attempt_back_merge(struct request_queue *q, struct request *req,
			    struct bio *bio);
bool blk_attempt_plug_merge(struct request_queue *q, struct bio *bio,
			    unsigned int *request_count,
			    struct request **same_queue_rq);
unsigned int blk_plug_queued_count(struct request_queue *q);

void blk_account_io_start(struct request *req, bool new_io);
void blk_account_io_completion(struct request *req, unsigned int bytes);
void blk_account_io_done(struct request *req);

/*
 * Internal atomic flags for request handling
 */
enum rq_atomic_flags {
	REQ_ATOM_COMPLETE = 0,
	REQ_ATOM_STARTED,
};

/*
 * EH timer and IO completion will both attempt to 'grab' the request, make
 * sure that only one of them succeeds
 */
static inline int blk_mark_rq_complete(struct request *rq)
{
	return test_and_set_bit(REQ_ATOM_COMPLETE, &rq->atomic_flags);
}

static inline void blk_clear_rq_complete(struct request *rq)
{
	clear_bit(REQ_ATOM_COMPLETE, &rq->atomic_flags);
}

/*
 * Internal elevator interface
 */
#define ELV_ON_HASH(rq) ((rq)->cmd_flags & REQ_HASHED)

void blk_insert_flush(struct request *rq);
#ifdef VENDOR_EDIT
/*Huacai.Zhou@PSW.BSP.Kernel.Performance, 2018-04-28, add foreground task io opt*/
extern int fg_count;
extern int both_count;
extern bool fg_debug;
extern unsigned int sysctl_fg_io_opt;
#endif /*VENDOR_EDIT*/

static inline struct request *__elv_next_request(struct request_queue *q)
{
	struct request *rq;
	struct blk_flush_queue *fq = blk_get_flush_queue(q, NULL);

	while (1) {
		if (!list_empty(&q->queue_head)) {
#ifdef VENDOR_EDIT
/*Huacai.Zhou@PSW.BSP.Kernel.Performance, 2018-04-28, add foreground task io opt*/
			if ( unlikely(!sysctl_fg_io_opt))
				rq = list_entry_rq(q->queue_head.next);
			else {
#ifdef CONFIG_PM
				if (!list_empty(&q->fg_head) && q->fg_count > 0 && (q->rpm_status == RPM_ACTIVE)) {
#else
				if (!list_empty(&q->fg_head) && q->fg_count > 0) {
#endif
					rq = list_entry(q->fg_head.next, struct request, fg_list);
					q->fg_count--;
				}
				else if (q->both_count > 0) {
						rq = list_entry_rq(q->queue_head.next);
						q->both_count--;
				}
				else {
					q->fg_count = q->fg_count_max;
					q->both_count = q->both_count_max;
					rq = list_entry_rq(q->queue_head.next);
				}
			}
#else
			rq = list_entry_rq(q->queue_head.next);
#endif /*VENDOR_EDIT*/
			return rq;
		}

		/*
		 * Flush request is running and flush request isn't queueable
		 * in the drive, we can hold the queue till flush request is
		 * finished. Even we don't do this, driver can't dispatch next
		 * requests and will requeue them. And this can improve
		 * throughput too. For example, we have request flush1, write1,
		 * flush 2. flush1 is dispatched, then queue is hold, write1
		 * isn't inserted to queue. After flush1 is finished, flush2
		 * will be dispatched. Since disk cache is already clean,
		 * flush2 will be finished very soon, so looks like flush2 is
		 * folded to flush1.
		 * Since the queue is hold, a flag is set to indicate the queue
		 * should be restarted later. Please see flush_end_io() for
		 * details.
		 */
		if (fq->flush_pending_idx != fq->flush_running_idx &&
				!queue_flush_queueable(q)) {
			fq->flush_queue_delayed = 1;
			return NULL;
		}
		if (unlikely(blk_queue_bypass(q)) ||
		    !q->elevator->type->ops.elevator_dispatch_fn(q, 0))
			return NULL;
	}
}

static inline void elv_activate_rq(struct request_queue *q, struct request *rq)
{
	struct elevator_queue *e = q->elevator;

	if (e->type->ops.elevator_activate_req_fn)
		e->type->ops.elevator_activate_req_fn(q, rq);
}

static inline void elv_deactivate_rq(struct request_queue *q, struct request *rq)
{
	struct elevator_queue *e = q->elevator;

	if (e->type->ops.elevator_deactivate_req_fn)
		e->type->ops.elevator_deactivate_req_fn(q, rq);
}

#ifdef CONFIG_FAIL_IO_TIMEOUT
int blk_should_fake_timeout(struct request_queue *);
ssize_t part_timeout_show(struct device *, struct device_attribute *, char *);
ssize_t part_timeout_store(struct device *, struct device_attribute *,
				const char *, size_t);
#else
static inline int blk_should_fake_timeout(struct request_queue *q)
{
	return 0;
}
#endif

int ll_back_merge_fn(struct request_queue *q, struct request *req,
		     struct bio *bio);
int ll_front_merge_fn(struct request_queue *q, struct request *req, 
		      struct bio *bio);
int attempt_back_merge(struct request_queue *q, struct request *rq);
int attempt_front_merge(struct request_queue *q, struct request *rq);
int blk_attempt_req_merge(struct request_queue *q, struct request *rq,
				struct request *next);
void blk_recalc_rq_segments(struct request *rq);
void blk_rq_set_mixed_merge(struct request *rq);
bool blk_rq_merge_ok(struct request *rq, struct bio *bio);
int blk_try_merge(struct request *rq, struct bio *bio);

void blk_queue_congestion_threshold(struct request_queue *q);

int blk_dev_init(void);


/*
 * Return the threshold (number of used requests) at which the queue is
 * considered to be congested.  It include a little hysteresis to keep the
 * context switch rate down.
 */
static inline int queue_congestion_on_threshold(struct request_queue *q)
{
	return q->nr_congestion_on;
}

/*
 * The threshold at which a queue is considered to be uncongested
 */
static inline int queue_congestion_off_threshold(struct request_queue *q)
{
	return q->nr_congestion_off;
}

extern int blk_update_nr_requests(struct request_queue *, unsigned int);

/*
 * Contribute to IO statistics IFF:
 *
 *	a) it's attached to a gendisk, and
 *	b) the queue had IO stats enabled when this request was started, and
 *	c) it's a file system request
 */
static inline int blk_do_io_stat(struct request *rq)
{
	return rq->rq_disk &&
	       (rq->cmd_flags & REQ_IO_STAT) &&
		(rq->cmd_type == REQ_TYPE_FS);
}

/*
 * Internal io_context interface
 */
void get_io_context(struct io_context *ioc);
struct io_cq *ioc_lookup_icq(struct io_context *ioc, struct request_queue *q);
struct io_cq *ioc_create_icq(struct io_context *ioc, struct request_queue *q,
			     gfp_t gfp_mask);
void ioc_clear_queue(struct request_queue *q);

int create_task_io_context(struct task_struct *task, gfp_t gfp_mask, int node);

/**
 * create_io_context - try to create task->io_context
 * @gfp_mask: allocation mask
 * @node: allocation node
 *
 * If %current->io_context is %NULL, allocate a new io_context and install
 * it.  Returns the current %current->io_context which may be %NULL if
 * allocation failed.
 *
 * Note that this function can't be called with IRQ disabled because
 * task_lock which protects %current->io_context is IRQ-unsafe.
 */
static inline struct io_context *create_io_context(gfp_t gfp_mask, int node)
{
	WARN_ON_ONCE(irqs_disabled());
	if (unlikely(!current->io_context))
		create_task_io_context(current, gfp_mask, node);
	return current->io_context;
}

/*
 * Internal throttling interface
 */
#ifdef CONFIG_BLK_DEV_THROTTLING
extern void blk_throtl_drain(struct request_queue *q);
extern int blk_throtl_init(struct request_queue *q);
extern void blk_throtl_exit(struct request_queue *q);
#else /* CONFIG_BLK_DEV_THROTTLING */
static inline void blk_throtl_drain(struct request_queue *q) { }
static inline int blk_throtl_init(struct request_queue *q) { return 0; }
static inline void blk_throtl_exit(struct request_queue *q) { }
#endif /* CONFIG_BLK_DEV_THROTTLING */

#endif /* BLK_INTERNAL_H */
