*NOTE*

The file format is highly volatile right now. Be sure to re-create the index/slab
files when updating versions.

# Eddy

High-performance, maintenence-light, caching library and tools.

The cache is comprised of two components: a fixed-size circular buffer and an
index that tracks entries into the buffer.

### Index

The index is a copy-on-write b+tree. This protects modifications to the cache
against corruption from crashes or improper API usage. All changes are made
through a transaction, which properly tracks garbage pages for safe reuse.

### Circular Buffer

Rather than rely on precise LRU caching, eddy uses a modified FIFO algorithm.
Active entries (those opened in an active transaction) are not evicted, and
will be kept in the cache until the next pass of the circular buffer.

This design has several advantages. The cache remains unfragmented and there is
no ongoing disk space utilization management. Additionally, this allows eddy to
be used as an index replay log.

The buffer stores up to two blobs for each entry: meta data and object data.
There is no formal reqiurements for either of the values, but they do have some
distinct characteristics. Meta data is specified during object creation, where as
object data is appended using the `ed_write` function. The sector size for meta
data is, by default, different than the sector size of object data. Meta data is
aligned to the system max alignment (typically 16 bytes). The object size is
configurable down to the max alignment, but it defaults to the system page size.

## Building

For building the full library and tools:

```bash
make
make install
```
    
If you only want parts, these may be invoked individually:

```bash
make bin # build only the executable tool
make lib # build the static and dynamic libraries
make static # build only the static library
make dynamic # build only the dynamic library and version symlinks
```

## Running

The build creates a tool name `eddy. This is used to create caches, but is also capable
of managing and manipulating contents.

For more information:

```bash
eddy help
eddy new --help
```

### Quick Start

```bash
eddy new -v ./stuff
echo "this is a test" | eddy set ./stuff test -t 200
eddy get ./stuff test
eddy get ./stuff test -i
eddy update ./stuff test -t -1
eddy ls ./stuff
```

### Example

```C
#include <string.h>
#include <err.h>
#include <eddy.h>

int
main(void)
{
	EdConfig cfg = ed_config_make("./stuff");
	EdCache *cache = NULL;
	int rc = ed_cache_open(&cache, &cfg);
	if (rc < 0) {
		errx(1, "failed to open index '%s': %s", cfg.index_path, ed_strerror(rc));
	}

	const char value[] = "some value";

	EdObjectAttr attr = ed_object_attr_make();
	attr.key = "thing";
	attr.keylen = strlen(attr.key);
	attr.datalen = sizeof(value);

	EdObject *obj;
	rc = ed_create(cache, &obj, &attr);
	if (rc < 0) {
		ed_cache_close(&cache);
		errx(1, "faild to create object: %s", ed_strerror(rc));
	}

	rc = ed_write(obj, value, sizeof(value));
	if (rc < 0) {
		warnx("faild to write object: %s", ed_strerror(rc));
	}

	ed_close(&obj);
	ed_cache_close(&cache);
}
```

### Thread Safety

Generally, eddy is geared towards parallel, multi-process access. Currently,
thread safety is very limited. It does not, and likely never will, support
opening multiple handles to the same cache within the same process. The plan
is to implement thread safety when using the _same_ cache handle in a single
process, however this work hasn't been completed yet. All access within a
handle is re-entrant however, so you may implement your own locking outside
of eddy. This also makes it safe to use separate handle to different caches
across threads.

### TODO
- [ ] document, document, document
- [ ] expose entry tagging for locking out regions
- [ ] implement thread safe handles or a thread safe wrapper API
- [ ] expose the internal transaction system for multiple updates

### Build Options

| Option | Description | Default |
| --- | --- | --- |
| `BUILD` | Build mode: `release` or `debug`. | `release` |
| `BUILD_MIME` | Build the MIME module. This is for both the command line tool and internal `mime.cache` database reader. | `yes` |
| `BUILD_MIMEDB` | Link the MIME database with the MIME module. This allows the MIME module to work without a local database. | `no` |
| `BUILD_DUMP` | Build the dump command. This is a debugging tool and will likely be disabled by default in the future. | `yes` |
| `OPT` | Optimization level of the build. | `3` for `release`, unset for `debug` |
| `LTO` | Enable link time optimizations. Additionally, this may be set to `amalg` which will produce an amalgamated source build rather than using the compiler LTO. The amalgamated build is always used for the static library any time LTO is enabled. | `yes` for `release`, `no` for `debug` |
| `DEBUG` | Build with debugging symbols. This allows `release` builds to retain debugging symbols. | `no` for optimized, `yes` otherwise |
| `DEBUG_MMAP` | Build with `mmap` tracking. | `no` |
| `CFLAGS` | The base compiler flags. These will be mixed into the required flags. | `-O$(OPT) -DNDEBUG` optimized, `-Wall -Werror` otherwise |
| `LDFLAGS` | The base linker flags. These will be mixed into the required flags.  | _no default_ |
| `PREFIX` | Base install directory. | `/usr/local` |
| `LIBNAME` | Base name of library products. | `eddy` |
| `BINNAME` | Name of the executable. | `eddy` |
| `PAGESIZE` | The target page size. Generally, this should not be changed. | result of `getconf PAGESIZE` |

<sup>1</sup>This also creates an amalgamated build for the static library.
Setting this to `amalg` will disable the compiler link time optimi

A cusomized build may be maintained by placing a `Build.mk` file in the
root of the source tree. If present, this will be included in the Makefile,
allowing the persistence of overrides to these build settings.

## Testing

To run the test suite, run:

```bash
make BUILD=debug test
```

The `debug` build mode enables page map/unmap tracking as well as sanitizer
checks. This will slow things down considerably.

There is a build stage for static analysis using `scan-build`:

```bash
make BUILD=debug analyze
```
