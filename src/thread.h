#ifndef ltask_thread_h
#define ltask_thread_h

struct thread {
	void (*func)(void *);
	void *ud;
};

static void* thread_start(struct thread * threads, int n, int usemainthread);
static void thread_join(void *handle, int n);
static void * thread_run(struct thread thread);
static void thread_wait(void *pid);

#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__)

#include <windows.h>

#if defined(DEBUGTHREADNAME)

static void inline
thread_setname(const char* name) {
	typedef HRESULT (WINAPI *SetThreadDescriptionProc)(HANDLE, PCWSTR);
	SetThreadDescriptionProc SetThreadDescription = (SetThreadDescriptionProc)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription");
	if (SetThreadDescription) {
		size_t size = (strlen(name)+1) * sizeof(wchar_t);
		wchar_t* wname = (wchar_t*)_alloca(size);
		mbstowcs(wname, name, size-2);
		SetThreadDescription(GetCurrentThread(), wname);
	}
#if defined(_MSC_VER)
	const DWORD MS_VC_EXCEPTION = 0x406D1388;
#pragma pack(push, 8)
	struct ThreadNameInfo {
		DWORD  type;
		LPCSTR name;
		DWORD  id;
		DWORD  flags;
	};
#pragma pack(pop)
	struct ThreadNameInfo info;
	info.type  = 0x1000;
	info.name  = name;
	info.id    = GetCurrentThreadId();
	info.flags = 0;
	__try {
		RaiseException(MS_VC_EXCEPTION, 0, sizeof(info)/sizeof(ULONG_PTR), (ULONG_PTR*)&info);
	} __except(EXCEPTION_EXECUTE_HANDLER) {}
#endif
}

#else

#define thread_setname(NAME)

#endif

static DWORD inline WINAPI
thread_function(LPVOID lpParam) {
	struct thread * t = (struct thread *)lpParam;
	t->func(t->ud);
	return 0;
}

static inline void *
thread_start(struct thread * threads, int n, int usemainthread) {
	int i;
	struct thread *mainthread = &threads[0];	// Use main thread for the 1st thread
	if (usemainthread) {
		++threads;
	}
	HANDLE *thread_handle = (HANDLE *)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,n*sizeof(HANDLE));
	for (i=0;i<n;i++) {
		thread_handle[i] = CreateThread(NULL, 0, thread_function, (LPVOID)&threads[i], 0, NULL);
		if (thread_handle[i] == NULL) {
			HeapFree(GetProcessHeap(), 0, thread_handle);
			return NULL;
		}
	}
	if (usemainthread)
		mainthread->func(mainthread->ud);
	return (void *)thread_handle;
}

static inline void
thread_join(void *handle, int n) {
	HANDLE *thread_handle = (HANDLE *)handle;
	WaitForMultipleObjects(n, thread_handle, TRUE, INFINITE);
	int i;
	for (i=0;i<n;i++) {
		CloseHandle(thread_handle[i]);
	}
	HeapFree(GetProcessHeap(), 0, thread_handle);
}

static DWORD inline WINAPI
thread_function_run(LPVOID lpParam) {
	struct thread * t = (struct thread *)lpParam;
	t->func(t->ud);
	HeapFree(GetProcessHeap(), 0, t);
	return 0;
}

static inline void *
thread_run(struct thread thread) {
	struct thread * t = (struct thread *)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY, sizeof(*t));
	*t = thread;
	HANDLE h = CreateThread(NULL, 0, thread_function_run, (LPVOID)t, 0, NULL);
	if (h == NULL) {
		HeapFree(GetProcessHeap(), 0, t);
	}
	return (void *)h;
}

static inline void
thread_wait(void *pid) {
	HANDLE h = (HANDLE)pid;
	WaitForSingleObject(h, INFINITE);
}

#elif defined(__EMSCRIPTEN__)

#include <emscripten/emscripten.h>
#include <emscripten/threading.h>
#include <emscripten/wasm_worker.h>
#include <stdint.h>
#include <stdlib.h>

#ifndef WASM_STACK_SIZE
#define WASM_STACK_SIZE (64 * 1024)
#endif

#define thread_setname(NAME)

#define thread_setnamef(...)

struct wasm_worker_context {
  struct thread *thread;
  emscripten_semaphore_t *semaphore;
};

struct wasm_thread_group {
  emscripten_wasm_worker_t *workers;
  struct wasm_worker_context *contexts;
  emscripten_semaphore_t semaphore;
};

static inline void
thread_function(int arg) {
  struct wasm_worker_context *ctx = (struct wasm_worker_context *)(intptr_t)arg;
  ctx->thread->func(ctx->thread->ud);
  emscripten_semaphore_release(ctx->semaphore, 1);
}

static void
wasm_thread_group_destroy(struct wasm_thread_group *group, int worker_count) {
  if (!group) {
    return;
  }
  if (group->workers) {
    for (int i = 0; i < worker_count; ++i) {
      if (group->workers[i] > 0) {
        emscripten_terminate_wasm_worker(group->workers[i]);
      }
    }
    free(group->workers);
  }
  if (group->contexts) {
    free(group->contexts);
  }
  free(group);
}

static inline void *
thread_start(struct thread *threads, int n, int usemainthread) {
  if (usemainthread) {
    emscripten_outf("usemainthread is not supported, use mainthread_api instead\n");
  }

  struct wasm_thread_group *group =
      (struct wasm_thread_group *)malloc(sizeof(*group));
  if (!group) {
    return NULL;
  }
  group->workers = NULL;
  group->contexts = NULL;

  emscripten_semaphore_init(&group->semaphore, 0);
  if (n <= 0) {
    return group;
  }

  group->workers =
      (emscripten_wasm_worker_t *)malloc(n * sizeof(emscripten_wasm_worker_t));
  group->contexts = (struct wasm_worker_context *)malloc(
      n * sizeof(struct wasm_worker_context));
  if (!group->workers || !group->contexts) {
    wasm_thread_group_destroy(group, 0);
    return NULL;
  }

  for (int i = 0; i < n; ++i) {
    group->contexts[i].thread = &threads[i];
    group->contexts[i].semaphore = &group->semaphore;
    emscripten_wasm_worker_t worker =
        emscripten_malloc_wasm_worker(WASM_STACK_SIZE);
    if (worker <= 0) {
      wasm_thread_group_destroy(group, i);
      return NULL;
    }
    group->workers[i] = worker;
    emscripten_wasm_worker_post_function_vi(worker, thread_function,
                                            (int)(intptr_t)&group->contexts[i]);
  }

  return group;
}

static inline void
thread_join(void *handle, int n) {
  struct wasm_thread_group *group = (struct wasm_thread_group *)handle;
  if (!group) {
    return;
  }

  while (emscripten_semaphore_try_acquire(&group->semaphore, n) == -1);

  wasm_thread_group_destroy(group, n);
}

static inline void *
thread_run(struct thread thread) {
  return thread_start(&thread, 1, 0);
}

static inline void 
thread_wait(void *pid) {
  return thread_join(pid, 1);
}

#else

#include <pthread.h>
#include <stdlib.h>

#if defined(DEBUGTHREADNAME)

#if defined(__linux__)
#	define LTASK_GLIBC (__GLIBC__ * 100 + __GLIBC_MINOR__)
#	if LTASK_GLIBC < 212
#		include <sys/prctl.h>
#	endif
#endif

#if defined(__FreeBSD__) || defined(__OpenBSD__)
#	include <pthread_np.h>
#endif

static void inline
thread_setname(const char* name) {
#if defined(__APPLE__)
	pthread_setname_np(name);
#elif defined(__linux__)
#	if LTASK_GLIBC >= 212
		pthread_setname_np(pthread_self(), name);
#	else
		prctl(PR_SET_NAME, name, 0, 0, 0);
#	endif
#elif defined(__NetBSD__)
	pthread_setname_np(pthread_self(), "%s", (void*)name);
#elif defined(__FreeBSD__) || defined(__OpenBSD__)
	pthread_set_name_np(pthread_self(), name);
#endif
}

#else

#define thread_setname(NAME)

#endif

static inline void *
thread_function(void * args) {
	struct thread * t = (struct thread *)args;
	t->func(t->ud);
	return NULL;
}

static inline void *
thread_start(struct thread *threads, int n, int usemainthread) {
	struct thread *mainthread = &threads[0];	// Use main thread for the 1st thread
	if (usemainthread) {
		++threads;
	}
	pthread_t *pid = (pthread_t *)malloc(n * sizeof(pthread_t));
	int i;
	for (i=0;i<n;i++) {
		if (pthread_create(&pid[i], NULL, thread_function, &threads[i])) {
			free(pid);
			return NULL;
		}
	}
	if (usemainthread)
		mainthread->func(mainthread->ud);

	return pid;
}

static inline void
thread_join(void *handle, int n) {
	pthread_t *pid = (pthread_t *)handle;
	int i;
	for (i=0;i<n;i++) {
		pthread_join(pid[i], NULL); 
	}
	free(handle);
}

static inline void *
thread_function_run(void * args) {
	struct thread * t = (struct thread *)args;
	t->func(t->ud);
	free(t);
	return NULL;
}

static inline void *
thread_run(struct thread thread) {
	pthread_t pid;
	struct thread *h = (struct thread *)malloc(sizeof(*h));
	*h = thread;
	if (pthread_create(&pid, NULL, thread_function_run, h)) {
		free(h);
		return NULL;
	}
	return (void *)pid;
}

static inline void
thread_wait(void *p) {
	pthread_t pid = (pthread_t)p;
	pthread_join(pid, NULL);
}

#endif

#if defined(DEBUGTHREADNAME)
static void inline
thread_setnamef(const char* fmt, ...) {
	char buf[256];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	thread_setname(buf);
}
#else
#define thread_setnamef(...)
#endif

#endif
