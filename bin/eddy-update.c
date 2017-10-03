#include "../lib/eddy-private.h"

static const char update_descr[] =
	"Update information about an object.";
static const char update_usage[] =
	"usage: eddy update {-t ttl | -e time} index key\n";
static EdOption update_opts[] = {
	{"ttl",     "ttl",  0, 't', "set the time-to-live in seconds"},
	{"expiry",  "time", 0, 'e', "set the expiry as a UNIX timestamp"},
	{0, 0, 0, 0, 0}
};

static int
update_run(const EdCommand *cmd, int argc, char *const *argv)
{
	EdConfig cfg = ed_config_make();
	EdCache *cache = NULL;
	char *end;
	int rc = 1;
	time_t t;
	bool has_ttl = false, has_expiry = false;

	int ch;
	while ((ch = ed_opt(argc, argv, cmd->opts, &cmd->usage)) != -1) {
		switch (ch) {
		case 't':
			if (has_expiry) { errx(1, "expiry cannot be combined with TTL"); }
			t = strtol(optarg, &end, 10);
			if (*end != '\0') { errx(1, "invalid number: %s", argv[optind-1]); }
			has_ttl = true;
			break;
		case 'e':
			if (has_ttl) { errx(1, "TTL cannot be combined with expiry"); }
			t = strtol(optarg, &end, 10);
			if (*end != '\0') { errx(1, "invalid number: %s", argv[optind-1]); }
			has_expiry = true;
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if (argc == 0) { errx(1, "index file path not provided"); }
	if (argc == 1) { errx(1, "key not provided"); }

	cfg.index_path = argv[0];

	rc = ed_cache_open(&cache, &cfg);
	if (rc < 0) { errx(1, "failed to open index '%s': %s", cfg.index_path, ed_strerror(rc)); }

	if (has_ttl) {
		rc = ed_update_ttl(cache, argv[1], strlen(argv[1]), t);
	}
	else if (has_expiry) {
		rc = ed_update_expiry(cache, argv[1], strlen(argv[1]), t);
	}

	if (rc < 0) {
		warnx("faild to update object: %s", ed_strerror(rc));
	}
	else if (rc == 0) {
		warnx("key not found");
	}

	ed_cache_close(&cache);
	return rc == 1 ? EXIT_SUCCESS : EXIT_FAILURE;
}
