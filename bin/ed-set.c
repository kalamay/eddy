#include "eddy-util.h"

#include <getopt.h>
#include <err.h>

static void
usage(const char *prog)
{
	const char *name = strrchr(prog, '/');
	name = name ? name + 1 : prog;
	fprintf(stderr,
			"usage: %s [-e EXP] [-m FILE] [-i PATH] PATH KEY [FILE]\n"
			"\n"
			"about:\n"
			"  Sets the contents of an object in the cache from stdin or a file.\n"
			"\n"
			"options:\n"
			"  -e EXP    set the time-to-live in seconds\n"
			"  -m FILE   set the object meta data from the contents of a file\n"
			"  -i PATH   path to index file (default is the cache path with \"-index\" suffix)\n"
			,
			name);
}

int
main(int argc, char **argv)
{
	EdConfig cfg = ed_config_make();
	EdCache *cache = NULL;
	EdInput meta = ed_input_make();
	EdInput data = ed_input_make();
	char *end;
	int rc;
	EdObjectAttr attr = { .expiry = -1 };

	int ch;
	while ((ch = getopt(argc, argv, ":hemi:")) != -1) {
		switch (ch) {
		case 'e':
			attr.expiry = strtol(optarg, &end, 10);
			if (*end != '\0') { errx(1, "invalid number: -%c", optopt); }
			break;
		case 'm':
			rc = ed_input_fread(&meta, optarg, UINT16_MAX);
			if (rc < 0) { errc(1, ed_ecode(rc), "failed to read MIME file"); }
			break;
		case 'i': cfg.index_path = optarg; break;
		case 'h': usage(argv[0]); return 0;
		case '?': errx(1, "invalid option: -%c", optopt);
		case ':': errx(1, "missing argument for option: -%c", optopt);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc == 0) { errx(1, "cache file not provided"); }
	cfg.cache_path = argv[0];

	if (argc == 1) { errx(1, "key not provided"); }
	attr.key = argv[1];
	attr.key_size = strlen(attr.key);

	argc -= 2;
	argv += 2;

	rc = ed_input_fread(&data, argc ? argv[0] : NULL, UINT32_MAX);
	if (rc < 0) { errc(1, ed_ecode(rc), "failed to read object file"); }

	rc = ed_cache_open(&cache, &cfg);
	if (rc < 0) { errx(1, "failed to open: %s", ed_strerror(rc)); }

	attr.meta = meta.data;
	attr.meta_size = meta.length;
	attr.object_size = data.length;

	EdObject *obj;
	rc = ed_create(cache, &obj, &attr);
	if (rc < 0) {
		warnx("faild to create object: %s", ed_strerror(rc));
	}

	ed_cache_close(&cache);
	return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

