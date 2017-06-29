#include <map>
#include <set>
#include <execinfo.h>

#undef ED_MMAP_DEBUG
#define ED_MMAP_DEBUG 1

extern "C" {
#include "eddy-private.h"
#include <pthread.h>
}

struct EdPgstack {
	EdBacktrace *bt;
	int rc;

	ED_INLINE EdPgstack() {
		rc = ed_backtrace_new(&bt);
	}

	~EdPgstack() {
		ed_backtrace_free(&bt);
	}

	void print() {
		if (rc >= 0) {
			int idx = ed_backtrace_index(bt, "ed_pguntrack");
			if (idx < 0) {
				idx = ed_backtrace_index(bt, "ed_pgtrack");
			}
			idx = idx < 0 ? 0 : idx + 1;
			ed_backtrace_print(bt, idx, stderr);
		}
	}
};

struct EdPgstate {
	EdPgno no;
	bool active;
	std::shared_ptr<EdPgstack> stack;
};

typedef std::map<uintptr_t, EdPgstate> EdPgtrack;

static pthread_rwlock_t track_lock = PTHREAD_RWLOCK_INITIALIZER;
static EdPgtrack *track = NULL;
static int track_errors = 0;

void 
ed_pgtrack(EdPgno no, uint8_t *pg, EdPgno count)
{
	if (pg == NULL) { return; }

	if (pthread_rwlock_wrlock(&track_lock) < 0) {
		fprintf(stderr, "*** failed to lock: %s\n", strerror(errno));
		abort();
	}

	try {
		if (track == NULL) { track = new EdPgtrack(); }

		auto stack = std::make_shared<EdPgstack>();
		uintptr_t k = (uintptr_t)pg, ke = k+(count*PAGESIZE);
		auto start = track->lower_bound(k);
		auto end = track->upper_bound(ke-PAGESIZE);

		for (auto it = start; it != end; ++it) {
			if (it->second.active) {
				fprintf(stderr, "*** address mapped multiple times: 0x%012" PRIxPTR "/%u\n",
						k, it->second.no);
				fprintf(stderr, "*** allocation stack:\n");
				it->second.stack->print();
				fprintf(stderr, "*** current stack:\n");
				stack->print();
				fprintf(stderr, "\n");
				track_errors++;
			}
		}

		for (; k < ke; k += PAGESIZE, no++) {
			EdPgstate state = { no, true, stack };
			auto result = track->emplace(k, state);
			if (!result.second) {
				result.first->second = state;
			}
		}
	}
	catch (...) {
	}

	if (pthread_rwlock_unlock(&track_lock) < 0) {
		fprintf(stderr, "*** failed to unlock: %s\n", strerror(errno));
		abort();
	}
}

void 
ed_pguntrack(uint8_t *pg, EdPgno count)
{
	if (pthread_rwlock_wrlock(&track_lock) < 0) {
		fprintf(stderr, "*** failed to lock: %s\n", strerror(errno));
		abort();
	}

	try {
		auto stack = std::make_shared<EdPgstack>();
		uintptr_t k = (uintptr_t)pg, ke = k+(count*PAGESIZE);
		if (track == NULL) {
			fprintf(stderr, "*** uninitialized address unmapped: 0x%012" PRIxPTR "/%u\n",
					k, *(EdPgno *)pg);
			stack->print();
			fprintf(stderr, "\n");
			track_errors++;
		}
		else {
			auto start = track->lower_bound(k);
			auto end = track->upper_bound(ke-PAGESIZE);

			std::set<uintptr_t> skip = {};
			for (auto it = start; it != end; ++it) {
				if (!it->second.active) {
					fprintf(stderr, "*** address unmapped multiple times: 0x%012" PRIxPTR "/%u\n",
							it->first, it->second.no);
					fprintf(stderr, "*** deallocation stack:\n");
					it->second.stack->print();
					fprintf(stderr, "*** current stack:\n");
					stack->print();
					fprintf(stderr, "\n");
					track_errors++;
					skip.emplace(it->first);
				}
			}

			for (; k < ke; k += PAGESIZE) {
				if (skip.find(k) != skip.end()) { continue; }
				EdPgstate state = { ((EdPg *)k)->no, false, stack };
				auto result = track->emplace(k, state);
				if (!result.second) {
					result.first->second = state;
				}
			}
		}
	}
	catch (...) {
	}

	if (pthread_rwlock_unlock(&track_lock) < 0) {
		fprintf(stderr, "*** failed to unlock: %s\n", strerror(errno));
		abort();
	}
}

int
ed_pgcheck(void)
{
	if (pthread_rwlock_rdlock(&track_lock) < 0) {
		fprintf(stderr, "*** failed to lock: %s\n", strerror(errno));
		abort();
	}

	int rc = track_errors;
	if (track != NULL) {
		for (auto it = track->begin(); it != track->end(); it++) {
			if (it->second.active) {
				fprintf(stderr, "*** address leaked: 0x%012" PRIxPTR "/%u\n",
						it->first, it->second.no);
				fprintf(stderr, "*** allocation stack:\n");
				it->second.stack->print();
				fprintf(stderr, "\n");
				rc++;
			}
		}
	}

	if (pthread_rwlock_unlock(&track_lock) < 0) {
		fprintf(stderr, "*** failed to unlock: %s\n", strerror(errno));
		abort();
	}

	return rc;
}

