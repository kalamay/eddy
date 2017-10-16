#include "../lib/eddy-private.h"

static const char ls_descr[] =
	"List keys in the cache.";
static const char ls_usage[] =
	"usage: eddy ls index [id]\n";
static EdOption ls_opts[] = {
	{0, 0, 0, 0, 0}
};

static int
ls_run(const EdCommand *cmd, int argc, char *const *argv)
{
	EdConfig cfg = ed_config_make();
	EdCache *cache = NULL;
	EdList *list = NULL;
	const EdObject *obj = NULL;
	const char *id = NULL;

	int ch;
	while ((ch = ed_opt(argc, argv, cmd->opts, &cmd->usage)) != -1) {}
	argc -= optind;
	argv += optind;

	if (argc == 0) { errx(1, "index file not provided"); }
	cfg.index_path = argv[0];

	if (argc > 0) { id = argv[1]; }

	int rc;

	rc = ed_cache_open(&cache, &cfg);
	if (rc < 0) { errx(1, "failed to open: %s", ed_strerror(rc)); }

	rc = ed_list_open(cache, &list, id);
	if (rc < 0) {
		warn("failed to start list: %s", ed_strerror(rc));
		goto done;
	}

	while ((rc = ed_list_next(list, &obj)) == 1) {
		printf("%s\t%ld\t%.*s\n", obj->id, ed_ttl(obj, -1), (int)obj->keylen, obj->key);
	}

	if (rc < 0) {
		warn("failed to iterate list: %s", ed_strerror(rc));
	}

done:
	ed_list_close(&list);
	ed_cache_close(&cache);
	return rc < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
