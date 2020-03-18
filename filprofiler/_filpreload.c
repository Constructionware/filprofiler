#define _GNU_SOURCE
#include "Python.h"
#include "frameobject.h"
#include <dlfcn.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

// Underlying APIs we're wrapping:
static void *(*underlying_real_mmap)(void *addr, size_t length, int prot,
                                     int flags, int fd, off_t offset) = 0;
static void (*underlying_real_free)(void *addr) = 0;

// Note whether we've been initialized yet or not:
static int initialized = 0;

static _Thread_local int will_i_be_reentrant = 0;
// Current thread's Python state:
static _Thread_local PyThreadState *tstate = NULL;

static void __attribute__((constructor)) constructor() {
  if (initialized) {
    return;
  }
  unsetenv("LD_PRELOAD");
  if (sizeof((void *)0) != sizeof((size_t)0)) {
    fprintf(stderr, "BUG: expected size of size_t and void* to be the same.\n");
    exit(1);
  }
  void *lib =
      dlopen(getenv("FIL_API_LIBRARY"), RTLD_NOW | RTLD_DEEPBIND | RTLD_GLOBAL);
  if (!lib) {
    fprintf(stderr, "Couldn't load libpymemprofile_api.so library: %s\n",
            dlerror());
    exit(1);
  }
  underlying_real_mmap = dlsym(RTLD_NEXT, "mmap");
  if (!underlying_real_mmap) {
    fprintf(stderr, "Couldn't load mmap(): %s\n", dlerror());
    exit(1);
  }
  underlying_real_free = dlsym(RTLD_NEXT, "free");
  if (!underlying_real_free) {
    fprintf(stderr, "Couldn't load free(): %s\n", dlerror());
    exit(1);
  }
  initialized = 1;
}

extern void *__libc_malloc(size_t size);
extern void *__libc_calloc(size_t nmemb, size_t size);
extern void pymemprofile_start_call(uint16_t parent_line_number,
                                    const char *filename, const char *funcname,
                                    uint16_t line_number);
extern void pymemprofile_finish_call();
extern void pymemprofile_new_line_number(uint16_t line_number);
extern void pymemprofile_reset();
extern void pymemprofile_dump_peak_to_flamegraph(const char *path);
extern void pymemprofile_add_allocation(size_t address, size_t length,
                                        uint16_t line_number);
extern void pymemprofile_free_allocation(size_t address);

__attribute__((visibility("default"))) void
fil_start_call(const char *filename, const char *funcname,
               uint16_t line_number) {
  if (!will_i_be_reentrant) {
    will_i_be_reentrant = 1;
    uint16_t parent_line_number = 0;
    if (tstate != NULL && tstate->frame != NULL && tstate->frame->f_back) {
      parent_line_number = PyFrame_GetLineNumber(tstate->frame->f_back);
    }

    pymemprofile_start_call(parent_line_number, filename, funcname,
                            line_number);
    will_i_be_reentrant = 0;
  }
}

__attribute__((visibility("default"))) void fil_finish_call() {
  if (!will_i_be_reentrant) {
    will_i_be_reentrant = 1;
    pymemprofile_finish_call();
    will_i_be_reentrant = 0;
  }
}

__attribute__((visibility("default"))) void
fil_new_line_number(uint16_t line_number) {
  if (!will_i_be_reentrant) {
    will_i_be_reentrant = 1;
    pymemprofile_new_line_number(line_number);
    will_i_be_reentrant = 0;
  }
}

__attribute__((visibility("default"))) void fil_reset() {
  if (!will_i_be_reentrant) {
    will_i_be_reentrant = 1;
    pymemprofile_reset();
    will_i_be_reentrant = 0;
  }
}

// Record current Python thread state in a thread-local, for easy retrieval; the
// Python APIs have many irrelevant-for-this-case assumptions about the GIL
// being held.
__attribute__((visibility("default"))) void fil_thread_started() {
  tstate = PyThreadState_Get();
}

__attribute__((visibility("default"))) void fil_thread_finished() {
  tstate = NULL;
}

__attribute__((visibility("default"))) void
fil_dump_peak_to_flamegraph(const char *path) {
  if (!will_i_be_reentrant) {
    will_i_be_reentrant = 1;
    pymemprofile_dump_peak_to_flamegraph(path);
    will_i_be_reentrant = 0;
  }
}

void add_allocation(size_t address, size_t size) {
  uint16_t line_number = 0;

  if (tstate != NULL && tstate->frame != NULL) {
    line_number = PyFrame_GetLineNumber(tstate->frame);
  }
  pymemprofile_add_allocation(address, size, line_number);
}

// Override memory-allocation functions:
__attribute__((visibility("default"))) void *malloc(size_t size) {
  void *result = __libc_malloc(size);
  if (!will_i_be_reentrant && initialized) {
    will_i_be_reentrant = 1;
    add_allocation((size_t)result, size);
    will_i_be_reentrant = 0;
  }
  return result;
}

__attribute__((visibility("default"))) void *calloc(size_t nmemb, size_t size) {
  void *result = __libc_calloc(nmemb, size);
  size_t allocated = nmemb * size;
  if (!will_i_be_reentrant && initialized) {
    will_i_be_reentrant = 1;
    add_allocation((size_t)result, allocated);
    will_i_be_reentrant = 0;
  }
  return result;
}

__attribute__((visibility("default"))) void free(void *addr) {
  if (!initialized) {
    // Well, we're going to leak a little memory, but, such is life...
    return;
  }
  underlying_real_free(addr);
  if (!will_i_be_reentrant) {
    will_i_be_reentrant = 1;
    pymemprofile_free_allocation((size_t)addr);
    will_i_be_reentrant = 0;
  }
}

/*
__attribute__ ((visibility("default"))) void* mmap(void *addr, size_t length,
int prot, int flags, int fd, off_t offset) { if (!initialized) {
constructor();
  }
  void* result = underlying_real_mmap(addr, length, prot, flags, fd, offset);
  fprintf(stdout, "MMAP!\n");
  if ((flags & (MAP_PRIVATE | MAP_ANONYMOUS)) && !will_i_be_reentrant &&
initialized) { will_i_be_reentrant = 1; update_memory_usage();
    will_i_be_reentrant = 0;
  }
  return result;
}
*/

int fil_tracer(PyObject *obj, PyFrameObject *frame, int what, PyObject *arg) {
  switch (what) {
  case PyTrace_CALL:
    fil_start_call(PyUnicode_AsUTF8(frame->f_code->co_filename),
                   PyUnicode_AsUTF8(frame->f_code->co_name), frame->f_lineno);
    break;
  case PyTrace_RETURN:
    fil_finish_call();
    if (frame->f_back == NULL) {
      // This thread is done.
      fil_thread_finished();
    }
  default:
    break;
  }
  return 0;
}

__attribute__((visibility("default"))) void register_fil_tracer() {
  fil_thread_started();
  PyEval_SetProfile(fil_tracer, Py_None);
}
