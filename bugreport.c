#include "cache.h"
#include "parse-options.h"
#include "stdio.h"
#include "strbuf.h"
#include "time.h"
#include "help.h"
#include <gnu/libc-version.h>
#include "run-command.h"
#include "config.h"
#include "bugreport-config-safelist.h"
#include "khash.h"

static void get_http_version_info(struct strbuf *http_info)
{
	struct child_process cp = CHILD_PROCESS_INIT;

	argv_array_push(&cp.args, "git");
	argv_array_push(&cp.args, "http-fetch");
	argv_array_push(&cp.args, "-V");
	if (capture_command(&cp, http_info, 0))
	    strbuf_addstr(http_info, "'git-http-fetch -V' not supported\n");
}

KHASH_INIT(cfg_set, const char*, int, 0, kh_str_hash_func, kh_str_hash_equal);

struct cfgset {
	kh_cfg_set_t set;
};

struct cfgset safelist;

static void cfgset_init(struct cfgset *set, size_t initial_size)
{
	memset(&set->set, 0, sizeof(set->set));
	if (initial_size)
		kh_resize_cfg_set(&set->set, initial_size);
}

static int cfgset_insert(struct cfgset *set, const char *cfg_key)
{
	int added;
	kh_put_cfg_set(&set->set, cfg_key, &added);
	printf("ESS: added %s\n", cfg_key);
	return !added;
}

static int cfgset_contains(struct cfgset *set, const char *cfg_key)
{
	khiter_t pos = kh_get_cfg_set(&set->set, cfg_key);
	return pos != kh_end(&set->set);
}

static void cfgset_clear(struct cfgset *set)
{
	kh_release_cfg_set(&set->set);
	cfgset_init(set, 0);
}

static void get_system_info(struct strbuf *sys_info)
{
	struct strbuf version_info = STRBUF_INIT;
	struct utsname uname_info;

	/* get git version from native cmd */
	strbuf_addstr(sys_info, "git version:\n");
	list_version_info(&version_info, 1);
	strbuf_addbuf(sys_info, &version_info);
	strbuf_complete_line(sys_info);

	/* system call for other version info */
	strbuf_addstr(sys_info, "uname -a: ");
	if (uname(&uname_info))
		strbuf_addf(sys_info, "uname() failed with code %d\n", errno);
	else
		strbuf_addf(sys_info, "%s %s %s %s %s\n",
			    uname_info.sysname,
			    uname_info.nodename,
			    uname_info.release,
			    uname_info.version,
			    uname_info.machine);

	strbuf_addstr(sys_info, "glibc version: ");
	strbuf_addstr(sys_info, gnu_get_libc_version());
	strbuf_complete_line(sys_info);

	strbuf_addf(sys_info, "$SHELL (typically, interactive shell): %s\n",
		    getenv("SHELL"));

	strbuf_addstr(sys_info, "git-http-fetch -V:\n");
	get_http_version_info(sys_info);
	strbuf_complete_line(sys_info);
}

static void gather_safelist()
{
	int index;
	int safelist_len = sizeof(bugreport_config_safelist) / sizeof(const char *);
	cfgset_init(&safelist, safelist_len);
	for (index = 0; index < safelist_len; index++)
		cfgset_insert(&safelist, bugreport_config_safelist[index]);

}

static int git_config_bugreport(const char *var, const char *value, void *cb)
{
	struct strbuf *config_info = (struct strbuf *)cb;

	if (cfgset_contains(&safelist, var))
		strbuf_addf(config_info,
			    "%s (%s) : %s\n",
			    var, config_scope_to_string(current_config_scope()),
			    value);

	return 0;
}

static void get_safelisted_config(struct strbuf *config_info)
{
	gather_safelist();
	git_config(git_config_bugreport, config_info);
	cfgset_clear(&safelist);
}

static const char * const bugreport_usage[] = {
	N_("git bugreport [-o|--output <file>]"),
	NULL
};

static int get_bug_template(struct strbuf *template)
{
	const char template_text[] = N_(
"Thank you for filling out a Git bug report!\n"
"Please answer the following questions to help us understand your issue.\n"
"\n"
"What did you do before the bug happened? (Steps to reproduce your issue)\n"
"\n"
"What did you expect to happen? (Expected behavior)\n"
"\n"
"What happened instead? (Actual behavior)\n"
"\n"
"What's different between what you expected and what actually happened?\n"
"\n"
"Anything else you want to add:\n"
"\n"
"Please review the rest of the bug report below.\n"
"You can delete any lines you don't wish to send.\n");

	strbuf_addstr(template, template_text);
	return 0;
}

static void get_header(struct strbuf *buf, const char *title)
{
	strbuf_addf(buf, "\n\n[%s]\n", title);
}

int cmd_main(int argc, const char **argv)
{
	struct strbuf buffer = STRBUF_INIT;
	struct strbuf report_path = STRBUF_INIT;
	FILE *report;
	time_t now = time(NULL);
	char *option_output = NULL;

	const struct option bugreport_options[] = {
		OPT_STRING('o', "output", &option_output, N_("path"),
			   N_("specify a destination for the bugreport file")),
		OPT_END()
	};
	argc = parse_options(argc, argv, "", bugreport_options,
			     bugreport_usage, 0);

	if (option_output) {
		strbuf_addstr(&report_path, option_output);
		strbuf_complete(&report_path, '/');
	}

	strbuf_addstr(&report_path, "git-bugreport-");
	strbuf_addftime(&report_path, "%F", gmtime(&now), 0, 0);
	strbuf_addstr(&report_path, ".txt");


	get_bug_template(&buffer);

	get_header(&buffer, "System Info");
	get_system_info(&buffer);

	get_header(&buffer, "Safelisted Config Info");
	get_safelisted_config(&buffer);

	report = fopen_for_writing(report_path.buf);
	strbuf_write(&buffer, report);
	fclose(report);

	launch_editor(report_path.buf, NULL, NULL);
	return 0;
}
