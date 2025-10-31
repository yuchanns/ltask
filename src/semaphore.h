#ifndef ltask_semaphore_h
#define ltask_semaphore_h

#if defined(__EMSCRIPTEN__)

#include <emscripten/wasm_worker.h>

struct sem {
	emscripten_semaphore_t sem;
};

static inline void
sem_init(struct sem *s) {
	emscripten_semaphore_init(&s->sem, 0);
}

static inline void
sem_deinit(struct sem *s) {
	(void)s;
}

static inline int
sem_wait(struct sem *s, int inf) {
	if (inf == 0) {
    // do not wait if failed
		return emscripten_semaphore_try_acquire(&s->sem, 1) == -1 ? -1 : 0;
	}
  // wait inf
	while (emscripten_semaphore_try_acquire(&s->sem, 1) == -1);
	return 0;
}

static inline void
sem_post(struct sem *s) {
	emscripten_semaphore_release(&s->sem, 1);
}


#else

#include "cond.h"

struct sem {
	struct cond c;
};

static inline void
sem_init(struct sem *s) {
	cond_create(&s->c);
}

static inline void
sem_deinit(struct sem *s) {
	cond_release(&s->c);
}

static inline int
sem_wait(struct sem *s, int inf) {
	// ignore inf, always wait inf
	cond_wait_begin(&s->c);
	cond_wait(&s->c);
	cond_wait_end(&s->c);
	// fail would return -1 (inf == 0)
	return 0;
}

static inline void
sem_post(struct sem *s) {
	cond_trigger_begin(&s->c);
	cond_trigger_end(&s->c, 1);
}

#endif

#endif
