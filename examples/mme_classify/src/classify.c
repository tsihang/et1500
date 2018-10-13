#include "oryx.h"
#include "file.h"
#include "tg.h"
#include "mme.h"
#include "main.h"
#include "classify.h"

extern struct oryx_task_t dequeue;

uint64_t nr_fopen_times;

vlib_threadvar_ctx_t vlib_threadvar_main[MAX_LQ_NUM];

/* Store those unknown entries. */
static vlib_mme_t *default_mme = NULL;

static __oryx_always_inline__
struct oryx_lq_ctx_t * fetch_lq(uint64_t lq_id, struct oryx_lq_ctx_t **lq) {
	vlib_main_t *vm = &vlib_main;
	/* fetch an LQ */
	vlib_threadvar_ctx_t *vtc = &vlib_threadvar_main[lq_id % vm->nr_threads];
	(*lq) = vtc->lq;
}

static __oryx_always_inline__
int write_lqe_data(vlib_file_t *f, const char *value, size_t valen)
{
	bool flush = 0;
	size_t nr_wb;
	
	/* flush to disk every 5000 entries. */
	if (f->entries % 50000 == 0)
		flush = 1;
	nr_wb = file_write(f, value, valen, flush);
	return nr_wb == valen ? 0 : 1;
}

static __oryx_always_inline__
void write_lqe(vlib_mme_t *mme, const struct lq_element_t *lqe)
{
	vlib_tm_grid_t vtg = {
		.start = 0,
		.end = 0
	};
	vlib_file_t *f, *fr;
	vlib_main_t *vm = &vlib_main;
	time_t tv_sec = LQE_EVENT_TIME(lqe);

	if (calc_tm_grid(&vtg, vm->threshold, tv_sec))
		return;
	
	/* lock MME */
	MME_LOCK(mme);

	f = &mme->file;
	fr = &mme->filer;

	/* for test */
	if (!fr->fp || fr->local_time != vtg.start)
		nr_fopen_times ++;
	
	mme_try_fopen(mme->pathr, mme->name, &vtg, fr);
	if (!fr->fp)
		goto finish;
	
	/* write raw. */
	if (!write_lqe_data (fr, (const char *)&lqe->value[0], lqe->valen)) {
		mme->nr_refcnt_r ++;
		mme->nr_refcnt_bytes_r += lqe->valen;
	}

	/* Anyway write those CDR with IMSI */	
	if (lqe->ul_flags & LQE_HAVE_IMSI) {
		mme_try_fopen(mme->path, mme->name, &vtg, f);
		if (!f->fp)
			goto finish;
		
		/* write CDR to disk ASAP */
		if (!write_lqe_data (fr, (const char *)&lqe->value[0], lqe->valen)) {
			mme->nr_refcnt ++;
			mme->nr_refcnt_bytes += lqe->valen;
		}
	}

finish:
	/* unlock MME */
	MME_UNLOCK(mme);
}

static void load_dictionary(vlib_main_t *vm)
{
	/* load dictionary */
	FILE *fp;
	const char *file = "src/cfg/dictionary";
	char line[256] = {0};
	char *p;
	int sep_refcnt = 0;
	char sep = ',';
	size_t step;
	vlib_mmekey_t	*mmekey;
	vlib_mme_t		*mme;
	void *s;
	char ip[32] = {0};
	char name[32] = {0};
	size_t nlen, iplen;
	int i;
	
	/* Overwrite MME dictionary. */
	if (vm->dictionary)
		file = vm->dictionary;
	
	fp = fopen(file, "r");
	if(!fp) {
		fprintf (stdout, "Cannot open %s \n", file);
		exit(0);
	}

	while (fgets (line, LINE_LENGTH, fp)) {

		for (p = &line[0], step = 0, sep_refcnt = 0;
				*p != '\0' && *p != '\n'; ++ p, step ++) {

			if (*p == sep)
				sep_refcnt ++;
			
			if (sep_refcnt == 1) {

				/* Parse MME name. */			
				memset (&name[0], 0, 32);
				memcpy (&name[0], &line[0], (p - line));

				/* skip the last sep ',' */
				++ p;
				
				/* Parse MME IP */
				memset (&ip[0], 0, 32);
				fmt_mme_ip(ip, p);

				nlen = strlen(name);
				iplen = strlen(ip);
				
				/* find same mme */
				s = oryx_htable_lookup(vm->mme_htable, ip, iplen);
				if (s) {
					mmekey = (vlib_mmekey_t *) container_of (s, vlib_mmekey_t, ip);
					if ((mme = mmekey->mme) != NULL) {
						fprintf (stdout, "find same mme %s by %s\n", mme->name, mmekey->ip);
					} else {
						fprintf (stdout, "Cannot assign a MME for MMEKEY! exiting ...\n");
						exit (0);
					}
				} else {
					/* find and alloc MME */
					if ((mme = mme_find(name, nlen)) == NULL) {
						mme = mme_alloc(name, nlen);
						if(mme) vm->nr_mmes ++;
					}
					/* alloc MMEKEY */
					mmekey = mmekey_alloc();
					if(!mmekey)
						break;

					mmekey->mme = mme;
					memcpy(&mmekey->ip[0], &ip[0], iplen);
					
					/* add mmekey dictionary to hash table.
					 * Actually, there are more than 1 IP for one MME.
					 * So, use IP of which MME as index to find unique MME> */
					BUG_ON(oryx_htable_add(vm->mme_htable, mmekey->ip, strlen(mmekey->ip)) != 0);
					fprintf (stdout, "%32s\"%16s\"%32s(%p)\n", "HTABLE ADD MME:", mme->name, mmekey->ip, mme);
				}
				break;
			}
		}
		memset (line, 0, 256);
	}
	
	fclose (fp);

	/* Prepare Default MME (the last one) */
	default_mme = mme_alloc("default", strlen("default"));
	if(default_mme) {
		vm->nr_mmes ++;
	}

	const char *pdir = vm->classdir;
	const char *sdir = vm->savdir;
		
	mkdir(pdir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
	mkdir(sdir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);

	/* Prepare Classify CSV files for each MME. */
	for (i = 0; i < vm->nr_mmes; i ++) {	
		mme = &nr_global_mmes[i];

		MME_LOCK(mme);
		sprintf (mme->path, "%s/%s", pdir, mme->name);
		sprintf (mme->pathr, "%s", sdir);
		fprintf (stdout, "mkdir %s \n", mme->path);
		mkdir(mme->path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
		
		/* Prepare a new CSV file.
		 * At this stage, mme->fp is definable NULL.
		 * To make sure that there is a valid file handler 
		 * before writing CSV to disk. */

		/* File for current stage */
		BUG_ON(mme->file.fp != NULL);
		MME_UNLOCK(mme);
	}

}

void classify_terminal(void)
{
	int i;
	struct oryx_lq_ctx_t *lq;
	vlib_threadvar_ctx_t *vtc;
	vlib_main_t *vm = &vlib_main;

	for (i = 0; i < vm->nr_threads; i ++) {
		vtc = &vlib_threadvar_main[i];
		lq = vtc->lq;
		fprintf (stdout, "LQ[%d], nr_eq_refcnt %ld, nr_dq_refcnt %ld, buffered %lu(%d)\n",
			i, lq->nr_eq_refcnt, lq->nr_dq_refcnt, (lq->nr_eq_refcnt - lq->nr_dq_refcnt), lq->len);
		oryx_lq_destroy(lq);
	}
}

void classify_runtime(void)
{
	int i;
	static uint64_t duration = 0;
	static size_t lqe_size = lq_element_size;
	static struct timeval start, end;
	static uint64_t nr_eq_swap_bytes_prev[MAX_LQ_NUM] = {0}, nr_eq_swap_elements_prev[MAX_LQ_NUM] = {0};
	static uint64_t nr_dq_swap_bytes_prev[MAX_LQ_NUM] = {0}, nr_dq_swap_elements_prev[MAX_LQ_NUM] = {0};
	uint64_t	nr_eq_swap_bytes_cur[MAX_LQ_NUM] = {0}, nr_eq_swap_elements_cur[MAX_LQ_NUM] = {0};
	uint64_t	nr_dq_swap_bytes_cur[MAX_LQ_NUM] = {0}, nr_dq_swap_elements_cur[MAX_LQ_NUM] = {0};	
	uint64_t	nr_cost_us = 0;
	uint64_t	eq[MAX_LQ_NUM] = {0}, dq[MAX_LQ_NUM] = {0};
	uint64_t	nr_eq_total_refcnt = 0, nr_dq_total_refcnt = 0;
	uint64_t	nr_classified_refcnt = 0;
	uint64_t	nr_unclassified_refcnt = 0;
	struct oryx_fmt_buff_t fb = FMT_BUFF_INITIALIZATION;

	char fmtstring[128] = {0};
	char pps_str[20], bps_str[20];
	struct oryx_lq_ctx_t *lq;
	vlib_threadvar_ctx_t *vtc;
	vlib_mme_t *mme;
	vlib_main_t *vm = &vlib_main;
	static oryx_file_t *fp;
	const char *lq_runtime_file = "/var/run/classify_runtime_summary.txt";
	char cat_null[128] = "cat /dev/null > ";

	strcat(cat_null, lq_runtime_file);
	system(cat_null);

	if(!fp) {
		fp = fopen(lq_runtime_file, "a+");
		if(!fp) fp = stdout;
	}

	gettimeofday(&end, NULL);
	/* cost before last time. */
	nr_cost_us = tm_elapsed_us(&start, &end);
	gettimeofday(&start, NULL);

	duration += (nr_cost_us / 1000000);

	/** Statistics for each QUEUE */
	for (i = 0; i < vm->nr_threads; i ++) {
		vtc = &vlib_threadvar_main[i];
		lq = vtc->lq;

		nr_unclassified_refcnt += vtc->nr_unclassified_refcnt;

		dq[i] = lq->nr_dq_refcnt;
		eq[i] = lq->nr_eq_refcnt;

		nr_eq_total_refcnt += eq[i];
		nr_dq_total_refcnt += dq[i];
		
		nr_eq_swap_bytes_cur[i]		=	(eq[i] * lqe_size) - nr_eq_swap_bytes_prev[i];
		nr_eq_swap_elements_cur[i] 	=	eq[i] - nr_eq_swap_elements_prev[i];
		
		nr_dq_swap_bytes_cur[i]		=	(dq[i] * lqe_size) - nr_dq_swap_bytes_prev[i];
		nr_dq_swap_elements_cur[i] 	=	dq[i] - nr_dq_swap_elements_prev[i];
		
		nr_eq_swap_bytes_prev[i]	=	(eq[i] * lqe_size);
		nr_eq_swap_elements_prev[i]	=	eq[i];
		
		nr_dq_swap_bytes_prev[i]	=	(dq[i] * lqe_size);
		nr_dq_swap_elements_prev[i]	=	dq[i];
	}

	/** Statistics for each MME */
	for (i = 0; i < vm->nr_mmes; i ++) {
		mme = &nr_global_mmes[i];
		nr_classified_refcnt += mme->nr_refcnt;
	}

	fprintf (fp, "Cost %lu us, duration %lu sec\n", nr_cost_us, (duration - epoch_time_sec));
	fprintf (fp, "\n");
	fflush(fp);
	
	fprintf (fp, "Classify Configurations\n");
	fprintf (fp, "\tdictionary: %s\n", vm->dictionary);
	fprintf (fp, "\tthreshold : %d minute(s)\n", vm->threshold);
	fprintf (fp, "\twarehouse : %s\n", vm->classdir);
	fprintf (fp, "\tparallel  : %d thread(s)\n", vm->nr_threads);
	fprintf (fp, "\n");
	fflush(fp);

	/* Overall classified ratio. */
	fprintf (fp, "Statistics Summary\n");
	fprintf (fp, "\tRx %lu, dispatched %lu (%.2f%%), no imsi %lu (%.2f%%)\n",
			vm->nr_rx_entries,
			vm->nr_rx_entries_dispatched, ratio_of(vm->nr_rx_entries_dispatched, vm->nr_rx_entries),
			vm->nr_rx_entries_without_imsi, ratio_of(vm->nr_rx_entries_without_imsi, vm->nr_rx_entries));
	fprintf (fp, "\tClassified %lu (%.2f%%), Unclassified: %lu (%.2f%%)\n",
			nr_classified_refcnt, ratio_of(nr_classified_refcnt, nr_eq_total_refcnt),
			nr_unclassified_refcnt, ratio_of(nr_unclassified_refcnt, nr_eq_total_refcnt));
	fprintf (fp, "\tOpened %lu handler(s), %lu\n", nr_handlers, nr_fopen_times);
	fprintf (fp, "\n");
	fflush(fp);
	
	fprintf (fp, "Statistics for %d thread(s)\n", vm->nr_threads);
	for (i = 0; i < vm->nr_threads; i ++) {
		fprintf (fp, "\tLQ[%d], blocked %lu element(s)\n", i, eq[i] - dq[i]);
		fprintf (fp, "\t\t enqueue %.2f%% %lu (pps %s, bps %s)\n",
			ratio_of(eq[i], nr_eq_total_refcnt),
			eq[i], oryx_fmt_speed(fmt_pps(nr_cost_us, nr_eq_swap_elements_cur[i]), pps_str, 0, 0), oryx_fmt_speed(fmt_bps(nr_cost_us, nr_eq_swap_bytes_cur[i]), bps_str, 0, 0));
		fprintf (fp, "\t\t dequeue %.2f%% %lu (pps %s, bps %s)\n",
			ratio_of(dq[i], nr_eq_total_refcnt),
			dq[i], oryx_fmt_speed(fmt_pps(nr_cost_us, nr_dq_swap_elements_cur[i]), pps_str, 0, 0), oryx_fmt_speed(fmt_bps(nr_cost_us, nr_dq_swap_bytes_cur[i]), bps_str, 0, 0));
	}
	fflush(fp);
	
	fprintf (fp, "\n\n");
	fprintf (fp, "Statistics for %d MME(s)\n", vm->nr_mmes);
	fprintf (fp, "%24s%32s\n", " ", "REFCNT");
	fprintf (fp, "%24s%32s\n", " ", "------");
	fflush(fp);

	for (i = 0; i < vm->nr_mmes; i ++) {
		oryx_format_reset(&fb);
		mme = &nr_global_mmes[i];

		/* format MME name */	
		oryx_format(&fb, "%24s", mme->name);

		/* format MME refcnt */
		sprintf (fmtstring, "%lu(%lu, %.2f%%)",
				mme->nr_refcnt_r, mme->nr_refcnt, ratio_of(mme->nr_refcnt, mme->nr_refcnt_r));
		oryx_format(&fb, "%32s", fmtstring);

		/* flush to file */
		oryx_format(&fb, "%s", "\n");
		fprintf(fp, FMT_DATA(fb), FMT_DATA_LENGTH(fb));
		fflush(fp);
	}

	oryx_format_free(&fb);
}

void do_dispatch(const char *value, size_t vlen)
{
	char *p;
	int sep_refcnt = 0;
	const char sep = ',';
	size_t step;
	static uint32_t lq_id = 0;
	struct lq_element_t *lqe;
	struct oryx_lq_ctx_t *lq;
	char data[1024] = {0}, time[32] = {0};
	bool keep_cpy = 1, event_start_time_cpy = 0, find_imsi = 0;
	int dlen = 0, tlen = 0;
	vlib_main_t *vm = &vlib_main;
	
	vm->nr_rx_entries ++;

	for (p = (const char *)&value[0], step = 0, sep_refcnt = 0;
				*p != '\0' && *p != '\n'; ++ p, step ++) {			

		/* skip entries without IMSI */
		if (!find_imsi && sep_refcnt == 5) {
			if (*p == sep) {
				vm->nr_rx_entries_without_imsi ++;
				//break;
			}
			else
				find_imsi = 1;
		}

		/* skip first 2 seps and copy event_start_time */
		if (sep_refcnt == 2) {
			event_start_time_cpy = 1;
		}

		/* skip last three columns */
		if (sep_refcnt == 43)
			keep_cpy = 0;

		/* valid entry dispatch ASAP */
		if (sep_refcnt == 45)
			goto dispatch;

		/* soft copy */
		if (keep_cpy)
			data[dlen ++] = *p;

		if (*p == sep)
			sep_refcnt ++;

		/* stop copy */
		if (sep_refcnt == 3) {
			event_start_time_cpy = 0;
		}

		/* soft copy.
		 * Time stamp is 13 bytes-long */
		if (event_start_time_cpy) {
			time[tlen ++] = *p;
		}
	}
	return;

dispatch:

	data[dlen - 1] = '\n';
	/* its terminating null byte ('\0'). */
	data[dlen] = '\0';
	/*
	fprintf (stdout, "no_imsi %d, len %d, strlen(data) %d,  %s, %s",
		no_imsi, dlen, strlen(data), data, p);
	*/
	lqe = lqe_alloc();
	if (!lqe)
		return;

	vm->nr_rx_entries_dispatched ++;

	/* soft copy. */
	memcpy((void *)&lqe->value[0], data, dlen);
	if(find_imsi)
		lqe->ul_flags |= LQE_HAVE_IMSI;
	lqe->valen = dlen;
	lqe->rawlen = vlen;
	sscanf(time, "%lu", &lqe->event_start_time);
	fmt_mme_ip(lqe->mme_ip, p);
	//fprintf (stdout, "event_start_time %s(%lu)\n", time, lqe->event_start_time);

	/* When round robbin used, CSV file must be locked while writing. */
	lq_id ++;
	if (vm->dispatch_mode == HASH)
		lq_id = oryx_js_hash(lqe->mme_ip, strlen(lqe->mme_ip));
	fetch_lq(lq_id, &lq);
	oryx_lq_enqueue(lq, lqe);
}

void do_classify(vlib_threadvar_ctx_t *vtc, struct lq_element_t *lqe)
{
	vlib_main_t *vm = &vlib_main;
	void *s;
	vlib_mmekey_t *mmekey;
	vlib_mme_t		*mme;

	/* 45 is the number of ',' in a CDR. */
	if (lqe->valen <= 45) {
		fprintf (stdout, "Invalid CDR with length %lu from \"%s\" \n", lqe->valen, (const char *)&lqe->mme_ip[0]);
#if defined(HAVE_CLASSIFY_DEBUG)
		/* debug ONLY */
		write_one_line(fp_unclassified_csv_entries, lqe->value, 1);
#endif
		vtc->nr_unclassified_refcnt ++;
		goto finish;
	}
	
	s = oryx_htable_lookup(vm->mme_htable, (const void *)&lqe->mme_ip[0], strlen((const char *)&lqe->mme_ip[0]));
	if (s) {
		mmekey = (vlib_mmekey_t *) container_of (s, vlib_mmekey_t, ip);
		mme = mmekey->mme;
		write_lqe(mme, lqe);
	} else {
		fprintf (stdout, "Cannot find a defined mmekey via IP \"%s\"\n", lqe->mme_ip);
		write_lqe(default_mme, lqe);
		vtc->nr_unclassified_refcnt ++;
#if defined(HAVE_CLASSIFY_DEBUG)
		/* debug ONLY */
		write_one_line(fp_unclassified_csv_entries, lqe->value, 1);
#endif
	}

finish:
	free((const void *)lqe);
}

void classify_tmr_handler(struct oryx_timer_t *tmr, int __oryx_unused_param__ argc, 
                char __oryx_unused_param__**argv)
{
	//fprintf (stdout, "%s running ...\n", tmr->sc_alias);
	classify_runtime();
}

void classify_env_init(vlib_main_t *vm)
{
	int i;
	uint32_t	lq_cfg = 0;
	vlib_threadvar_ctx_t *vtc;

	epoch_time_sec = time(NULL);
	
	vm->mme_htable = oryx_htable_init(DEFAULT_HASH_CHAIN_SIZE, 
								mmekey_hval, mmekey_cmp, mmekey_free, 0);	

	for (i = 0; i < vm->nr_threads; i ++) {
		vtc = &vlib_threadvar_main[i];
		vtc->unique_id = i;
		/* new queue */
		oryx_lq_new("A new list queue", lq_cfg, (void **)&vtc->lq);
		oryx_lq_dump(vtc->lq);

		char name[64] = {0};
		sprintf (name, "Dequeue Task%d", i);
		struct oryx_task_t *t = malloc (sizeof(struct oryx_task_t));
		BUG_ON(t == NULL);
		memset(t, 0, sizeof (struct oryx_task_t));
		memcpy(t, &dequeue, sizeof (struct oryx_task_t));
		t->lcore_mask = INVALID_CORE;
		//t->lcore_mask = (1 << (i + ENQUEUE_LCORE_ID));
		t->ul_prio = KERNEL_SCHED;
		t->argc = 1;
		t->argv = vtc;
		t->sc_alias = strdup(name);
		oryx_task_registry(t);
	}
	
	load_dictionary(vm);

	struct oryx_timer_t *tmr = oryx_tmr_create (1, "Classify Runtime TMR", TMR_OPTIONS_PERIODIC | TMR_OPTIONS_ADVANCED,
											  classify_tmr_handler, 0, NULL, 1000);
	oryx_tmr_start(tmr);

}

