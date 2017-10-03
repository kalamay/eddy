#include "../lib/eddy-private.h"
#define DEFAULT_SIZE "4096p"

static const char new_descr[] =
	"Creates a new cache index and slab.";
static const char new_usage[] =
	"usage: eddy new [-v] [-f] [-c] [-s size[k|m|g|t|p]] [-S slab] index\n"
	"\n"
	"Sizes are expressed as numbers with optional size modifiers.\n"
	"Supported size modifiers are:\n"
	"  k  kibibytes (1024 bytes)\n"
	"  m  mebibytes (1048576 bytes)\n"
	"  g  gibibytes (1073741824 bytes)\n"
	"  t  tebibytes (1099511627776 bytes)\n"
	"  p  pages (" ED_STR(PAGESIZE) " bytes)\n";
static EdOption new_opts[] = {
	{"size",       "size", 0, 's', "size of the file (default " DEFAULT_SIZE ")"},
	{"slab",       "path", 0, 'S', "path to slab file (default is the index path with \"-slab\" suffix)"},
	{"verbose",    NULL,   0, 'v', "enable verbose messaging"},
	{"force",      NULL,   0, 'f', "force creation of a new cache file"},
	{"checksum",   NULL,   0, 'c', "track crc32 checksums"},
	{"page-align", NULL,   0, 'p', "force file data to be page aligned"},
	{0, 0, 0, 0, 0}
};

static int
new_run(const EdCommand *cmd, int argc, char *const *argv)
{
	char *size_arg = DEFAULT_SIZE;
	EdConfig cfg = { .flags = ED_FCREATE|ED_FALLOCATE };

	int ch;
	while ((ch = ed_opt(argc, argv, cmd->opts, &cmd->usage)) != -1) {
		switch (ch) {
		case 'v': cfg.flags |= ED_FVERBOSE; break;
		case 'f': cfg.flags |= ED_FREPLACE; break;
		case 'c': cfg.flags |= ED_FCHECKSUM; break;
		case 'p': cfg.flags |= ED_FPAGEALIGN; break;
		case 's': size_arg = optarg; break;
		case 'S': cfg.slab_path = optarg; break;
		}
	}
	argc -= optind;
	argv += optind;

	if (!ed_parse_size(size_arg, &cfg.slab_size)) {
		errx(1, "-s must be a valid positive number");
	}

	if (argc == 0) { errx(1, "index file path not provided"); }
	cfg.index_path = argv[0];

	EdCache *cache;
	int rc = ed_cache_open(&cache, &cfg);
	if (rc < 0) {
		fprintf(stderr, "failed to open cache: %s\n", ed_strerror(rc));
		return EXIT_FAILURE;
	}
	ed_cache_close(&cache);
	return EXIT_SUCCESS;
}