#ifndef INCLUDED_EDDY_PRIVATE_H
#define INCLUDED_EDDY_PRIVATE_H

#include "eddy.h"

#include <stdatomic.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <inttypes.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <pthread.h>
#include <assert.h>

#if defined(__linux__)
# include <linux/fs.h>
#elif defined(__APPLE__) || defined(__FreeBSD__)
# include <sys/disk.h>
#else
# error Platform not supported
#endif

#ifndef ED_ALLOC_COUNT
# define ED_ALLOC_COUNT 16
#endif

#ifndef ED_MAX_ALIGN
# ifdef __BIGGEST_ALIGNMENT__
#  define ED_MAX_ALIGN __BIGGEST_ALIGNMENT__
# else
#  define ED_MAX_ALIGN 16
# endif
#endif

#define ED_LOCAL __attribute__((visibility ("hidden")))
#define ED_INLINE static inline __attribute__((always_inline))



/** @brief  Seconds from an internal epoch */
typedef uint32_t EdTime;

typedef struct EdLck EdLck;

typedef uint32_t EdPgno;
typedef uint64_t EdBlkno;
typedef struct EdPg EdPg;
typedef struct EdPgAlloc EdPgAlloc;
typedef struct EdPgAllocHdr EdPgAllocHdr;
typedef struct EdPgFree EdPgFree;
typedef struct EdPgTail EdPgTail;
typedef struct EdPgGc EdPgGc;
typedef struct EdPgGcList EdPgGcList;
typedef struct EdPgNode EdPgNode;

typedef struct EdGc EdGc;

typedef struct EdBpt EdBpt;

typedef uint64_t EdTxnId;
typedef struct EdTxn EdTxn;
typedef struct EdTxnType EdTxnType;
typedef struct EdTxnDb EdTxnDb;
typedef struct EdTxnNode EdTxnNode;

typedef struct EdIdx EdIdx;
typedef struct EdIdxHdr EdIdxHdr;

typedef struct EdNodeBlock EdNodeBlock;
typedef struct EdNodeKey EdNodeKey;
typedef struct EdObjectHdr EdObjectHdr;



/**
 * @defgroup  lck  Lock Module
 *
 * A combined thread and file lock with shared and exclusive locking modes.
 *
 * @{
 */

typedef enum EdLckType {
	ED_LCK_SH = F_RDLCK,
	ED_LCK_EX = F_WRLCK,
	ED_LCK_UN = F_UNLCK,
} EdLckType;

struct EdLck {
	struct flock f;
	pthread_rwlock_t rw;
};

/**
 * @brief  Initializes the lock in an unlocked state.
 *
 * The lock controls both thread-level, and file-level reader/writer locking.
 * A byte range is specified for file locking, allowing multiple independant
 * locks per file.
 *
 * @param  lck  Pointer to a lock value
 * @param  start  Starting byte off
 * @param  len  Number of bytes from #start
 */
ED_LOCAL void
ed_lck_init(EdLck *lck, off_t start, off_t len);

/**
 * @brief  Cleans up any resources for the lock.
 *
 * It is expected that the lock has been released by #ed_lck().
 *
 * @param  lck  Pointer to a lock value
 */
ED_LOCAL void
ed_lck_final(EdLck *lck);

/**
 * @brief  Controls the lock state.
 *
 * This is used to both aquires a lock (shared or exlusive) and release it.
 * It is undefined behavior to acquire a lock multiple times. The lock must
 * be released before #ed_lck_final().
 * 
 * Supported values for #type are:
 *   - #ED_LCK_SH
 *       - Acquires a read-only shared lock.
 *   - #ED_LCK_EX
 *       - Acquires a read-write exclusive lock.
 *   - #ED_LCK_UN
 *       - Unlocks either the shared or exclusive lock.
 * 
 * Supported flags are:
 *   - #ED_FNOTLCK
 *       - Disable thread locking.
 *   - #ED_FNOFLCK
 *       - Disable file locking.
 *   - #ED_FNOBLOCK
 *       - Return EAGAIN if locking would block.
 *
 * When unlocking, the #ED_FNOTLCK and #ED_FNOFLCK flags must be equivalent
 * to those used when locking.
 *
 * @param  lck  Pointer to a lock value
 * @param  fd  Open file descriptor to lock
 * @param  type  The lock action to take
 * @param  flags  Modify locking behavior
 */
ED_LOCAL int
ed_lck(EdLck *lck, int fd, EdLckType type, uint64_t flags);

/** @} */



/**
 * @defgroup  pg  Page Module
 *
 * Page utility functions.
 *
 * @{
 */

#define ED_PG_INDEX     UINT32_C(0x58444e49)
#define ED_PG_FREE_HEAD UINT32_C(0x44485246)
#define ED_PG_FREE_CHLD UINT32_C(0x44435246)
#define ED_PG_BRANCH    UINT32_C(0x48435242)
#define ED_PG_LEAF      UINT32_C(0x4641454c)
#define ED_PG_OVERFLOW  UINT32_C(0x5245564f)
#define ED_PG_GC        UINT32_C(0x4c4c4347)

#define ED_PG_NONE UINT32_MAX
#define ED_BLK_NONE UINT64_MAX

#define ED_PG_FREE_COUNT ((PAGESIZE - sizeof(EdPg) - sizeof(EdPgno)) / sizeof(EdPgno))

/**
 * @brief  In-memory node to wrap a b+tree node
 *
 * This captures the parent node when mapping each subsequent page, allowing
 * simpler reverse-traversal withough the complexity of storing the relationship.
 * Additionally, the node tracks the advisory dirty state of the page. When
 * commited, this will sync the page unless #ED_FNOSYNC is used. If the page is
 * not marked for commiting, the page will instead be returned as a free page
 * to the page allocator.
 */
struct EdPgNode {
	union {
		EdPg *page;       /**< Mapped page */
		EdBpt *tree;      /**< Mapped page as a tree */
	};
	EdPgNode *parent;     /**< Parent node */
	uint16_t pindex;      /**< Index of page in the parent */
	uint8_t dirty;        /**< Dirty state of the page */
	bool commit;          /**< Commit or free page */
};

ED_LOCAL   void * ed_pg_map(int fd, EdPgno no, EdPgno count);
ED_LOCAL      int ed_pg_unmap(void *p, EdPgno count);
ED_LOCAL      int ed_pg_sync(void *p, EdPgno count, uint64_t flags, uint8_t lvl);
ED_LOCAL   void * ed_pg_load(int fd, EdPg **pgp, EdPgno no);
ED_LOCAL     void ed_pg_unload(EdPg **pgp);
ED_LOCAL     void ed_pg_mark(EdPg *pg, EdPgno *no, uint8_t *dirty);

/** @} */



/**
 * @defgroup  gc  Page Garbage Collector
 *
 * @{
 */

/**
 * @brief  Type to hold all in-memory and on-disk garbage collector state
 */
struct EdGc {
	EdPgGc *head;
	EdPgGc *tail;
	EdPgno *headno;
	EdPgno *tailno;
};

/**
 * @brief  Initializes a garbage collector
 * @param  gc  Pointer to initialize
 * @param  headno  Location to store the head page number
 * @param  tailno  Location to store the tail page number
 */
ED_LOCAL void
ed_gc_init(EdGc *gc, EdPgno *headno, EdPgno *tailno);

/**
 * @brief  Finalizes a garbage collector
 *
 * This will unmap an internal pages.
 *
 * @param  gc  Pointer to initialized garbage collector
 */
ED_LOCAL void
ed_gc_final(EdGc *gc);

/**
 * @brief  Pushes an array of page numbers into the garbage collector
 *
 * This call transfers all pages to the garbage collector atomically. That is,
 * all pages are recorded, or none are. Multiple calls do not have a durability
 * guarantees even within a single lock acquisition. Ownership of all pages
 * is transfered to the garbage collector, and any external pointers should be
 * considered invalid after a successful call.
 *
 * @param  gc  Garbage collector pointer
 * @param  alloc  Page allocator
 * @param  xid  The transaction ID that is discarding these pages
 * @param  pg  Array of page objects
 * @param  n  Number of entries in the page number array
 * @returns  0 on success, <0 on error
 */
ED_LOCAL int
ed_gc_put(EdGc *gc, EdPgAlloc *alloc, EdTxnId xid, EdPg **pg, EdPgno n);

/**
 * @brief  Runs the garbage collector
 *
 * This will transfer garbage pages back the free pool if their associated
 * transaction ids are less than #xid.
 *
 * @param  gc  Garbage collector pointer
 * @param  alloc  Page allocator
 * @param  xid  The lowest active transaction ID
 * @param  limit  The maximum number of transaction IDs to free
 * @returns  0 on success, <0 on error
 */
ED_LOCAL int
ed_gc_run(EdGc *gc, EdPgAlloc *alloc, EdTxnId xid, int limit);

/** @} */



/**
 * @defgroup  pgalloc  Page Allocator Module
 *
 * This implement the file-backed page allocator used by the index. This is
 * pulled out into its own module primarily to aid in testability.
 *
 * @{
 */

struct EdPgAlloc {
	EdPgAllocHdr *hdr;
	void *pg;
	EdPgFree *free;
	uint64_t flags;
	int fd;
	uint8_t dirty, free_dirty;
	bool from_new;
};

/**
 * @brief  Allocates a page from the underlying file
 *
 * This will first attempt a lock-free allocation from any tail pages. If there
 * are not enough pages, the continued allocation will depend on the #exclusive
 * access to the allocator.
 *
 * Without #exclusive access, the number of pages successfully acquired will be
 * returned, but no furthur allocation will take place. That is, without
 * #exclusive access, fewer than the number of pages requested may be returned.
 *
 * With #exclusive access, pages will be allocated from the free list,
 * reclaiming previously freed pages. If there are not enough free pages
 * available, the file will be expanded.
 *
 * @param  alloc  Page allocator
 * @param  p  Array to store allocated pages into
 * @param  n  Number of pages to allocate
 * @param  exclusive  If this allocation call has exclusive access to the allocator
 * @return  >0 the number of pages allocated from the tail or free list,
 *          <0 error code
 */
ED_LOCAL int
ed_pg_alloc(EdPgAlloc *alloc, EdPg **, EdPgno n, bool exclusive);

/**
 * @brief  Frees disused page objects
 *
 * This will not reclaim the disk space used, however, it will become
 * available for later allocations.
 *
 * Exclusive access to allocator is assumed for this call.
 *
 * @param  alloc  Page allocator
 * @param  p  Array of pages objects to deallocate
 * @param  n  Number of pages to deallocate
 */
ED_LOCAL void
ed_pg_free(EdPgAlloc *alloc, EdPg **p, EdPgno n);

/**
 * @brief  Frees disused page numbers
 * @see ed_pg_free
 * @param  alloc  Page allocator
 * @param  p  Array of pages numbers to deallocate
 * @param  n  Number of pages to deallocate
 */
ED_LOCAL void
ed_pgno_free(EdPgAlloc *alloc, EdPgno *pages, EdPgno n);

/**
 * @brief  Constructs a new page allocator on disk
 *
 * This is primarily used for testing the page allocator. In typical usage, the
 * page allocator is embedded in the index.
 * 
 * @param  alloc  Page allocator to initialize
 * @param  path  Path on disk to store the allocator
 * @param  meta  Meta data space to reserve with the page allocator
 * @param  flags  Flags for operational options
 * @return 0 on succes, <0 on error
 */
ED_LOCAL int
ed_pg_alloc_new(EdPgAlloc *alloc, const char *path, size_t meta, uint64_t flags);

/**
 * @brief  Initializes a page allocator from an existing mapped header
 * @param  alloc  Page allocator to initialize
 * @param  hdr  Allocator header embedded in another object
 * @param  fd  File descriptor for the file on disk
 * @param  flags  Flags for operational options
 */
ED_LOCAL void
ed_pg_alloc_init(EdPgAlloc *alloc, EdPgAllocHdr *hdr, int fd, uint64_t flags);

/**
 * @brief  Closes the allocator and releases all resources
 * @param  alloc  Page allocator
 */
ED_LOCAL void
ed_pg_alloc_close(EdPgAlloc *alloc);

/**
 * @brief  Syncs the page allocator to disk
 * @param  alloc  Page allocator
 */
ED_LOCAL void
ed_pg_alloc_sync(EdPgAlloc *alloc);

/**
 * @brief  Gets the meta data space requested from #ed_pg_alloc_new
 * @param  alloc  Page allocator
 * @return  Pointer to meta data space
 */
ED_LOCAL void *
ed_pg_alloc_meta(EdPgAlloc *alloc);

/**
 * @brief  Gets the meta data space requested from #ed_pg_alloc_new
 * @param  alloc  Page allocator
 * @return  Object with the list of free pages
 */
ED_LOCAL EdPgFree *
ed_pg_alloc_free_list(EdPgAlloc *alloc);

/** @} */



#if ED_MMAP_DEBUG
/**
 * @defgroup  pgtrack  Page Usage Tracking Module
 *
 * When enabled, tracks improper page uses including leaked pages, double
 * unmaps, and unmapping uninitialized addresses.
 *
 * @{
 */

ED_LOCAL     void ed_pg_track(EdPgno no, uint8_t *pg, EdPgno count);
ED_LOCAL     void ed_pg_untrack(uint8_t *pg, EdPgno count);
ED_LOCAL      int ed_pg_check(void);

/** @} */
#endif



#if ED_BACKTRACE
/**
 * @defgroup  backtrace  Backtrace Module
 *
 * When enabled, provides stack traces for error conditions with the page
 * tracker and transactions.
 *
 * @{
 */

typedef struct EdBacktrace EdBacktrace;

ED_LOCAL      int ed_backtrace_new(EdBacktrace **);
ED_LOCAL     void ed_backtrace_free(EdBacktrace **);
ED_LOCAL     void ed_backtrace_print(EdBacktrace *, int skip, FILE *);
ED_LOCAL      int ed_backtrace_index(EdBacktrace *, const char *name);

/** @} */
#endif



/**
 * @defgroup  bpt  B+Tree Module
 *
 * @{
 */

typedef enum EdBptApply {
	ED_BPT_NONE,
	ED_BPT_INSERT,
	ED_BPT_REPLACE,
	ED_BPT_DELETE
} EdBptApply;

typedef int (*EdBptPrint)(const void *, char *buf, size_t len);

ED_LOCAL   size_t ed_bpt_capacity(size_t esize, size_t depth);
ED_LOCAL      int ed_bpt_find(EdTxn *txn, unsigned db, uint64_t key, void **ent);
ED_LOCAL      int ed_bpt_first(EdTxn *txn, unsigned db, void **ent);
ED_LOCAL      int ed_bpt_next(EdTxn *txn, unsigned db, void **ent);
ED_LOCAL      int ed_bpt_loop(const EdTxn *txn, unsigned db);
ED_LOCAL      int ed_bpt_set(EdTxn *txn, unsigned db, const void *ent, bool replace);
ED_LOCAL      int ed_bpt_del(EdTxn *txn, unsigned db);
ED_LOCAL     void ed_bpt_apply(EdTxn *txn, unsigned db, const void *ent, EdBptApply);
ED_LOCAL     void ed_bpt_print(EdBpt *, int fd, size_t esize, FILE *, EdBptPrint);
ED_LOCAL      int ed_bpt_verify(EdBpt *, int fd, size_t esize, FILE *);

/** @} */



/**
 * @brief  txn  Transaction Module
 *
 * This implements the transaction system for working with multiple database
 * b+trees. These aren't "real" transactions, in that only a single change
 * operation is supported per database. However, single changes made to
 * multiple databases are handled as a single change. This is all that's
 * needed for the index, so the simplicity is preferrable for the moment.
 * The design of the API is intended to accomodate full transactions if that
 * does become necessary.
 *
 * @{
 */

#define ED_TXN_MAX_TYPE 16

#define ED_TXN_CLOSED 0
#define ED_TXN_OPEN 1

/**
 * @brief  Transaction database instance information
 *
 * This describes each database involved in the transaction. An array of these
 * is passed to #ed_txn_new().
 */
struct EdTxnType {
	EdPgno *no;           /**< Pointer to the page number for the root of the b+tree */
	size_t entry_size;    /**< Size in bytes of the entry value in the b+tree */
};

/**
 * @brief  Transaction database reference
 *
 * This is the object allocated for each database involved in the transaction.
 * Additionally, it acts as a cursor for iterating through records in the tree.
 */
struct EdTxnDb {
	EdPgNode *head;       /**< First node searched */
	EdPgNode *tail;       /**< Current tail node */
	EdPgno *root;         /**< Pointer to page number of root node */
	uint64_t key;         /**< Key searched for */
	void *start;          /**< Pointer to the first entry */
	void *entry;          /**< Pointer to the entry in the leaf */
	size_t entry_size;    /**< Size in bytes of the entry */
	uint32_t entry_index; /**< Index of the entry in the leaf */
	int nsplits;          /**< Number of nodes requiring splits for an insert */
	int match;            /**< Return code of the search */
	int nmatches;         /**< Number of matched keys so far */
	int nloops;           /**< Number of full iterations */
	bool haskey;          /**< Mark if the cursor started with a find key */
	bool caninsert;       /**< Mark if the cursor either started as, or has become, read-only */
	void *scratch;        /**< New entry content */
	EdBptApply apply;     /**< Replace, insert, or delete entry */
};

/**
 * @brief  Transaction object
 *
 * This holds all information for an active or reset transaction. Once
 * allocated, the transaction cannot be changed. However, it may be reset and
 * used for multiple transactions agains the same database set.
 */
struct EdTxn {
	EdLck *lck;           /**< Reference to shared lock */
	EdPgAlloc *alloc;     /**< Page allocator */
	EdPg **pg;            /**< Array to hold allocated pages */
	unsigned npg;         /**< Number of pages allocated */
	unsigned npgused;     /**< Number of pages used */
	EdTxnNode *nodes;     /**< Linked list of node arrays */
	uint64_t cflags;      /**< Critical flags required during #ed_txn_commit() or #ed_txn_close() */
	bool isrdonly;        /**< Was #ed_txn_open() called with #ED_FRDONLY */
	bool isopen;          /**< Has #ed_txn_open() been called */
	unsigned ndb;         /**< Number of search objects */
	EdTxnDb db[1];        /**< Search object flexible array member */
};

struct EdTxnNode {
	EdTxnNode *next;      /**< Next chunk of nodes */
	unsigned nnodes;      /**< Length of node array */
	unsigned nnodesused;  /**< Number of nodes used */
	EdPgNode nodes[1];    /**< Flexible array member of node wrapped pages */
};

/**
 * @brief  Allocates a new transaction for working with a specific set of databases.
 *
 * The object is deallocated when calling #ed_txn_commit() or #ed_txn_close().
 * However, the transaction may be reused by passing #ED_FRESET to either of
 * these functions.
 *
 * @param  txnp  Indirect pointer to a assign the allocation to
 * @param  alloc  A page allocator instance
 * @param  lck  An initialized lock object
 * @param  type  An Array of #EdTxnType structs
 * @param  ntype  The number of #EdTxnType structs
 * @return  0 on success <0 on error
 */
ED_LOCAL int
ed_txn_new(EdTxn **txnp, EdPgAlloc *alloc, EdLck *lck, EdTxnType *type, unsigned ntype);

/**
 * @brief  Starts an allocated transaction
 *
 * This must be called before reading or writing to any database objects.
 * 
 * Supported flags are:
 *   - #ED_FRDONLY
 *       - The operation will not write any changes.
 *   - #ED_FNOTLCK
 *       - Disable thread locking.
 *   - #ED_FNOFLCK
 *       - Disable file locking.
 *   - #ED_FNOBLOCK
 *       - Return EAGAIN if the required lock would block.
 *
 * @param  txn  Closed transaction object
 * @param  flags  Behavior modification flags
 * @return  0 on success <0 on error
 */
ED_LOCAL int
ed_txn_open(EdTxn *txn, uint64_t flags);

/**
 * @brief  Commits the changes to each database
 * 
 * Supported flags are:
 *   - #ED_FNOSYNC
 *       - Disable file syncing.
 *   - #ED_FASYNC
 *       - Don't wait for pages to complete syncing.
 *   - #ED_FRESET
 *       - Reset the transaction for another use.
 *
 * @param  txnp  Indirect pointer an open transaction
 * @return  0 on success <0 on error
 */
ED_LOCAL int
ed_txn_commit(EdTxn **txnp, uint64_t flags);

/**
 * @brief  Closes the transaction and abandons any pending changes
 * 
 * Supported flags are:
 *   - #ED_FNOSYNC
 *       - Disable file syncing.
 *   - #ED_FASYNC
 *       - Don't wait for pages to complete syncing.
 *   - #ED_FRESET
 *       - Reset the transaction for another use.
 *
 * @param  txnp  Indirect pointer an open transaction
 * @param  flags  Behavior modification flags
 * @return  0 on success <0 on error
 */
ED_LOCAL void
ed_txn_close(EdTxn **txnp, uint64_t flags);

/**
 * @brief  Maps a page wrapped into a node
 *
 * The page is unmapped and possibly synced when the transaction is complete.
 *
 * @param  txn  Transaction object
 * @param  no  Page number to map
 * @param  par  Parent node or `NULL`
 * @param  pidx  Index of the page in the parent
 * @param  out  Node pointer to assign to
 * @return  0 on success <0 on error
 */
ED_LOCAL int
ed_txn_map(EdTxn *txn, EdPgno no, EdPgNode *par, uint16_t pidx, EdPgNode **out);

/**
 * @brief  Retrieves the next allocated page wrapped into a node
 *
 * The number of pages needed is determined by the modifications made to the
 * databases. Requesting too many pages will result in an abort.
 *
 * @param  txn  Transaction object
 * @param  par  Parent node or `NULL`
 * @param  pidx  Index of the page in the parent
 * @return  Node object
 */
ED_LOCAL EdPgNode *
ed_txn_alloc(EdTxn *txn, EdPgNode *par, uint16_t pidx);

/**
 * @brief  Gets the #EdTxnDb object for the numbered database
 * @param  txn  Transaction object
 * @param  db  Database number, 0-based
 * @param  reset  Reset the search and modification state
 */
ED_LOCAL EdTxnDb *
ed_txn_db(EdTxn *txn, unsigned db, bool reset);

/** @} */



/**
 * @defgroup  idx  Index Module
 *
 * @{
 */

struct EdIdx {
	EdLck lck;
	EdPgAlloc alloc;
	uint64_t flags;
	uint64_t seed;
	EdTimeUnix epoch;
	EdIdxHdr *hdr;
	EdBpt *blocks;
	EdBpt *keys;
	EdTxn *txn;
};

ED_LOCAL      int ed_idx_open(EdIdx *, const EdConfig *cfg, int *slab_fd);
ED_LOCAL     void ed_idx_close(EdIdx *);
ED_LOCAL      int ed_idx_get(EdIdx *, const void *key, size_t len, EdObject *obj);
ED_LOCAL      int ed_idx_put(EdIdx *, const void *key, size_t len, EdObject *obj);
ED_LOCAL      int ed_idx_lock(EdIdx *, EdLckType type);
ED_LOCAL      int ed_idx_stat(EdIdx *, FILE *, int flags);

/** @} */



/**
 * @defgroup  rnd  Random Module
 *
 * @{
 */

ED_LOCAL      int ed_rnd_open(void);
ED_LOCAL  ssize_t ed_rnd_buf(int fd, void *buf, size_t len);
ED_LOCAL      int ed_rnd_u64(int fd, uint64_t *);

/** @} */



/**
 * @defgroup  time  Time Module
 *
 * @{
 */

#define ED_TIME_DELETE ((EdTime)0)
#define ED_TIME_MAX ((EdTime)UINT32_MAX-1)
#define ED_TIME_INF ((EdTime)UINT32_MAX)

/**
 * @brief  Converts a UNIX time to an internal epoch
 * @param  epoch  The internal epoch as a UNIX timestamp
 * @param  at  The UNIX time to convert
 * @return  internal time representation
 */
ED_LOCAL EdTime
ed_time_from_unix(EdTimeUnix epoch, EdTimeUnix at);

/**
 * @brief  Converts an internal time to UNIX time
 * @param  epoch  The internal epoch as a UNIX timestamp
 * @param  at  The internal time to convert
 * @return  UNIX time representation
 */
ED_LOCAL EdTimeUnix
ed_time_to_unix(EdTimeUnix epoch, EdTime at);

/**
 * @brief  Gets the current time in UNIX representation
 * @return  UNIX time representation
 */
ED_LOCAL EdTimeUnix
ed_now_unix(void);

/**
 * @brief  Gets the internal expiry as a time-to-live from a UNIX time
 * @param  epoch  The internal epoch as a UNIX timestamp
 * @param  ttl  Time-to-live in seconds or <0 for infinite
 * @param  at  The UNIX time to convert
 * @return  internal time representation
 */
ED_LOCAL EdTime
ed_expiry_at(EdTimeUnix epoch, EdTimeTTL ttl, EdTimeUnix at);

/**
 * @brief  Gets the time-to-live from the a UNIX time
 * @param  epoch  The internal epoch as a UNIX timestamp
 * @param  exp  The absolute expiration in internal time
 * @param  at  The UNIX time to convert
 * @return  Time-to-live in seconds
 */
ED_LOCAL EdTimeTTL
ed_ttl_at(EdTimeUnix epoch, EdTime exp, EdTimeUnix at);

/**
 * @brief  Tests if the internal time is expired against the a UNIX time
 * @param  epoch  The internal epoch as a UNIX timestamp
 * @param  exp  The absolute expiration in internal time
 * @param  at  The UNIX time to compare
 * @return  true if the value is expired
 */
ED_LOCAL bool
ed_expired_at(EdTimeUnix epoch, EdTime exp, EdTimeUnix at);

/** @} */



/**
 * @defgroup  util  Utility Functions
 *
 * @{
 */

#define ED_COUNT_SIZE(n, size) (((n) + ((size)-1)) / (size))
#define ED_ALIGN_SIZE(n, size) (ED_COUNT_SIZE(n, size) * (size))

#define ed_count_max(n) ED_COUNT_SIZE(n, ED_MAX_ALIGN)
#define ed_align_max(n) ED_ALIGN_SIZE(n, ED_MAX_ALIGN)

#define ed_count_pg(n) ED_COUNT_SIZE(n, PAGESIZE)
#define ed_align_pg(n) ED_ALIGN_SIZE(n, PAGESIZE)

#define ed_fsave(f) ((uint32_t)((f) & UINT64_C(0x00000000FFFFFFFF)))
#define ed_fopen(f) ((f) & UINT64_C(0xFFFFFFFF00000000))

#define ed_len(arr) (sizeof(arr) / sizeof((arr)[0]))

#if BYTE_ORDER == LITTLE_ENDIAN
# define ed_b16(v) __builtin_bswap16(v)
# define ed_l16(v) (v)
# define ed_b32(v) __builtin_bswap32(v)
# define ed_l32(v) (v)
# define ed_b64(v) __builtin_bswap64(v)
# define ed_l64(v) (v)
#elif BYTE_ORDER == BIG_ENDIAN
# define ed_b16(v) (v)
# define ed_l16(v) __builtin_bswap16(v)
# define ed_b32(v) (v)
# define ed_l32(v) __builtin_bswap32(v)
# define ed_b64(v) (v)
# define ed_l64(v) __builtin_bswap64(v)
#endif

#define ed_ptr_b32(T, b, off) ((const T *)((const uint8_t *)(b) + ed_b32(off)))

#define ed_verbose(f, ...) do { \
	if ((f) & ED_FVERBOSE) { fprintf(stderr, __VA_ARGS__); fflush(stderr); } \
} while (0)

#define ED_IS_FILE(mode) (S_ISREG(mode))
#define ED_IS_DEVICE(mode) (S_ISCHR(mode) || S_ISBLK(mode))
#define ED_IS_MODE(mode) (ED_IS_FILE(mode) || ED_IS_DEVICE(mode))

/**
 * @brief  Change the size of the open file descriptor.
 *
 * This attempts to use the most efficient means for expanding a file for the
 * host platform.
 *
 * @param  fd  File descriptor open for writing
 * @param  size  The target size of the file
 * @return  0 on success <0 on failure
 */
ED_LOCAL int
ed_mkfile(int fd, off_t size);

/**
 * @brief  64-bit seeded hash function.
 *
 * @param  val  Bytes to hash
 * @param  len  Number of bytes to hash
 * @param  seed  Seed for the hash family
 * @return  64-bit hash value
 */
ED_LOCAL uint64_t
ed_hash(const uint8_t *val, size_t len, uint64_t seed);

static inline uint32_t __attribute__((unused))
ed_fetch32(const void *p)
{
	uint32_t val;
	memcpy(&val, p, sizeof(val));
	return val;
}

static inline uint64_t __attribute__((unused))
ed_fetch64(const void *p)
{
	uint64_t val;
	memcpy(&val, p, sizeof(val));
	return val;
}

static inline unsigned __attribute__((unused))
ed_power2(unsigned p)
{
	if (p > 0) {
		p--;
		p |= p >> 1;
		p |= p >> 2;
		p |= p >> 4;
		p |= p >> 8;
#if UINT_MAX > UINT16_MAX
		p |= p >> 16;
#if UINT_MAX > UINT32_MAX
		p |= p >> 32;
#endif
#endif
		p++;
	}
	return p;
}

/** @} */



#define ED_NODE_BLOCK_COUNT ((PAGESIZE - sizeof(EdBpt)) / sizeof(EdNodeBlock))
#define ED_NODE_KEY_COUNT ((PAGESIZE - sizeof(EdBpt)) / sizeof(EdNodeKey))

struct EdCache {
	EdIdx idx;
	atomic_int ref;
	int fd;
	size_t bytes_used;
	size_t pages_used;
};

struct EdObject {
	EdCache *cache;
	time_t expiry;
	const void *data;
	const void *key;
	const void *meta;
	uint16_t keylen;
	uint16_t metalen;
	uint32_t datalen;
	EdObjectHdr *hdr;
};

#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wpadded"

struct EdPg {
	EdPgno no;
	uint32_t type;
};

struct EdPgFree {
	EdPg base;
	EdPgno count;
	EdPgno pages[ED_PG_FREE_COUNT];
};

struct EdPgTail {
	EdPgno start;
	EdPgno off;
};

/**
 * @brief  Flexible array of pages removed from a given transaction
 */
struct EdPgGcList {
	EdTxnId  xid;      /**< Transaction id that freed these pages */
	EdPgno   npages;   /**< Number of pages in the array, or UINT32_MAX for the next list */
	EdPgno   pages[1]; /**< Flexible array of the pages to free */
};

/**
 * @brief  Linked list of pages pending reclamation
 */
struct EdPgGc {
	EdPg         base;   /**< Page number and type */
	EdPgno       next;   /**< Linked list of furthur gc pages */
	uint16_t     head;   /**< Data offset for the first active list object */
	uint16_t     tail;   /**< Data offset for the last list object */
	uint16_t     remain; /**< Data space after the last list object */
	uint8_t     _pad[6];
#define ED_PG_GC_DATA (PAGESIZE - sizeof(EdPg) - sizeof(EdPgno) - 12)
	uint8_t      data[ED_PG_GC_DATA];
};

/** Maximum number of pages for a list in a new gc page */
#define ED_PG_GC_LIST_MAX ((ED_PG_GC_DATA - sizeof(EdPgGcList)) / sizeof(EdPgno) + 1)

struct EdPgAllocHdr {
	uint16_t size_page;
	uint16_t size_block;
	EdPgno free_list;
	_Atomic EdPgTail tail;
};

struct EdIdxHdr {
	EdPg base;
	char magic[4];
	char endian;
	uint8_t mark;
	uint16_t version;
	uint32_t flags;
	uint32_t pos;
	EdPgno key_tree;
	EdPgno block_tree;
	uint64_t seed;
	EdTimeUnix epoch;
	EdPgAllocHdr alloc;
	uint8_t size_align;
	uint8_t alloc_count;
	uint8_t _pad[2];
	EdPgno slab_page_count;
	uint64_t slab_ino;
	char slab_path[1024];
};

struct EdObjectHdr {
	uint16_t keylen;
	uint16_t metalen;
	uint32_t datalen;
};

/**
 * @brief  Page type for b+tree branches and leaves
 *
 * For leaf nodes, the #nkeys field is the number of entries in the leaf.
 * Branches use this field for the number of keys. Howevever, there is one
 * more child page pointer than the number of keys.
 *
 * The #data size is the remaining space of a full page after subtracting the
 * size of the header fields. Currently, #data is guaranteed to be 8-byte
 * aligned.
 *
 * For branch nodes, the layout of the data segment looks like:
 *
 * 0      4       12     16       24
 * +------+--------+------+--------+-----+----------+------+
 * | P[0] | Key[0] | P[1] | Key[1] | ... | Key[N-1] | P[N] |
 * +------+--------+------+--------+-----+----------+------+
 *
 * The page pointer values (P) are 32-bit numbers as a page offset for the
 * child page. The keys are 64-bit numbers. Each key on P[0] is less than
 * Key[0]. Each key on P[1] is greater or equal to Key[0], and so on. These
 * values are guaranteed to have 4-byte alignment and do not need special
 * handling to read. The Key values are 64-bit and may not be 8-byte aligned.
 * These values need to be acquired using the `ed_fetch64` to accomodate
 * unaligned reads.
 *
 * Leaf nodes use the data segment as an array of entries. Each entry *must*
 * start with a 64-bit key.
 */
struct EdBpt {
	EdPg     base;              /**< Page number and type */
	EdTxnId  xid;               /**< Transaction ID that allocated this page */
	EdPgno   next;              /**< Overflow leaf pointer */
	uint16_t nkeys;             /**< Number of keys in the node */
	uint8_t  _pad[2];
#define ED_BPT_DATA (PAGESIZE - sizeof(EdPg) - sizeof(EdTxnId) - sizeof(EdPgno) - 4)
	uint8_t  data[ED_BPT_DATA]; /**< Tree-specific data for nodes (8-byte aligned) */
};

struct EdNodeBlock {
	EdBlkno block; // XXX last block of the entry?
	EdPgno no;
	uint32_t exp;
};

struct EdNodeKey {
	uint64_t hash;
	EdBlkno no;
	EdPgno count;
	uint32_t exp;
};

#pragma GCC diagnostic pop

#endif

