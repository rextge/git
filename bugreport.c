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
#include "run-command.h"

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

static void get_populated_hooks(struct strbuf *hook_info)
{
	/*
	 * Doesn't look like there is a list of all possible hooks; so below is
	 * a transcription of `git help hook`.
	 */
	const char *hooks = "applypatch-msg,"
			    "pre-applypatch,"
			    "post-applypatch,"
			    "pre-commit,"
			    "pre-merge-commit,"
			    "prepare-commit-msg,"
			    "commit-msg,"
			    "post-commit,"
			    "pre-rebase,"
			    "post-checkout,"
			    "post-merge,"
			    "pre-push,"
			    "pre-receive,"
			    "update,"
			    "post-receive,"
			    "post-update,"
			    "push-to-checkout,"
			    "pre-auto-gc,"
			    "post-rewrite,"
			    "sendemail-validate,"
			    "fsmonitor-watchman,"
			    "p4-pre-submit,"
			    "post-index-changex";
	struct string_list hooks_list = STRING_LIST_INIT_DUP;
	struct string_list_item *iter = NULL;
	int nongit_ok;

	setup_git_directory_gently(&nongit_ok);

	if (nongit_ok) {
		strbuf_addstr(hook_info,
			"not run from a git repository - no hooks to show\n");
		return;
	}

	string_list_split(&hooks_list, hooks, ',', -1);

	for_each_string_list_item(iter, &hooks_list) {
		if (find_hook(iter->string)) {
			strbuf_addstr(hook_info, iter->string);
			strbuf_complete_line(hook_info);
		}
	}
}

static int is_hex(const char *string, size_t count)
{
	for (; count; string++, count--) {
		if (!isxdigit(*string))
			return 0;
	}
	return 1;
}

static void get_loose_object_summary(struct strbuf *obj_info) {
	struct dirent *d = NULL;
	DIR *dir, *subdir = NULL;
	size_t dir_len;
	struct strbuf dirpath = STRBUF_INIT;

	strbuf_addstr(&dirpath, get_object_directory());
	strbuf_complete(&dirpath, '/');

	dir = opendir(dirpath.buf);
	if (!dir) {
		strbuf_addf(obj_info, "could not open object directory '%s'\n",
			    dirpath.buf);
		strbuf_release(&dirpath);
		return;
	}

	dir_len = dirpath.len;

	while ((d = readdir(dir))) {
		int object_count = 0;
		char subdir_name[3];

		if (d->d_type != DT_DIR)
			continue;

		if ((strlen(d->d_name) != 2) || (!is_hex(d->d_name, 2)))
			continue;

		/* copy directory name + \0 */
		memcpy(subdir_name, d->d_name, 3);

		strbuf_setlen(&dirpath, dir_len);
		strbuf_addstr(&dirpath, d->d_name);

		subdir = opendir(dirpath.buf);
		if (!subdir)
			continue;
		while ((d = readdir(subdir)))
			if (d->d_type == DT_REG)
				object_count++;

		closedir(subdir);

		strbuf_addf(obj_info, "%s: %d\n", subdir_name, object_count);
	}


	closedir(dir);
	strbuf_release(&dirpath);
}

static void get_packed_object_summary(struct strbuf *obj_info)
{
	struct strbuf dirpath = STRBUF_INIT;
	struct dirent *d;
	DIR *dir = NULL;

	strbuf_addstr(&dirpath, get_object_directory());
	strbuf_complete(&dirpath, '/');
	strbuf_addstr(&dirpath, "pack/");

	dir = opendir(dirpath.buf);
	if (!dir) {
		strbuf_addf(obj_info, "could not open packed object directory '%s'\n",
			    dirpath.buf);
		strbuf_release(&dirpath);
		return;
	}

	while ((d = readdir(dir))) {
		strbuf_addbuf(obj_info, &dirpath);
		strbuf_addstr(obj_info, d->d_name);
		strbuf_complete_line(obj_info);
	}

	closedir(dir);
	strbuf_release(&dirpath);
}

static void list_contents_of_dir_recursively(struct strbuf *contents,
				      	     struct strbuf *dirpath)
{
	struct dirent *d;
	DIR *dir;
	size_t path_len;

	dir = opendir(dirpath->buf);
	if (!dir)
		return;

	strbuf_complete(dirpath, '/');
	path_len = dirpath->len;

	while ((d = readdir(dir))) {
		if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, ".."))
			continue;

		strbuf_addbuf(contents, dirpath);
		strbuf_addstr(contents, d->d_name);
		strbuf_complete_line(contents);

		if (d->d_type == DT_DIR) {
			strbuf_addstr(dirpath, d->d_name);
			list_contents_of_dir_recursively(contents, dirpath);
		}
		strbuf_setlen(dirpath, path_len);
	}

	closedir(dir);
}

static void get_object_info_summary(struct strbuf *obj_info)
{
	struct strbuf dirpath = STRBUF_INIT;

	strbuf_addstr(&dirpath, get_object_directory());
	strbuf_complete(&dirpath, '/');
	strbuf_addstr(&dirpath, "info/");

	list_contents_of_dir_recursively(obj_info, &dirpath);

	strbuf_release(&dirpath);
}

static void get_alternates_summary(struct strbuf *alternates_info)
{
	struct strbuf alternates_path = STRBUF_INIT;
	struct strbuf alternate = STRBUF_INIT;
	FILE *file;
	size_t exists = 0, broken = 0;

	strbuf_addstr(&alternates_path, get_object_directory());
	strbuf_complete(&alternates_path, '/');
	strbuf_addstr(&alternates_path, "info/alternates");

	file = fopen(alternates_path.buf, "r");
	if (!file) {
		strbuf_addstr(alternates_info, "No alternates file found.\n");
		strbuf_release(&alternates_path);
		return;
	}

	while (strbuf_getline(&alternate, file) != EOF) {
		if (!access(alternate.buf, F_OK))
			exists++;
		else
			broken++;
	}

	strbuf_addf(alternates_info,
		    "%zd alternates found (%zd working, %zd broken)\n",
		    exists + broken,
		    exists,
		    broken);

	fclose(file);
	strbuf_release(&alternate);
	strbuf_release(&alternates_path);
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

	get_header(&buffer, "Configured Hooks");
	get_populated_hooks(&buffer);

	get_header(&buffer, "Loose Object Counts");
	get_loose_object_summary(&buffer);

	get_header(&buffer, "Packed Object Summary");
	get_packed_object_summary(&buffer);

	get_header(&buffer, "Object Info Summary");
	get_object_info_summary(&buffer);

	get_header(&buffer, "Alternates");
	get_alternates_summary(&buffer);

	report = fopen_for_writing(report_path.buf);
	strbuf_write(&buffer, report);
	fclose(report);

	launch_editor(report_path.buf, NULL, NULL);
	return 0;
}
