#include "builtin.h"
#include "parse-options.h"
#include "stdio.h"
#include "strbuf.h"
#include "time.h"

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

	report = fopen_for_writing(report_path.buf);
	strbuf_write(&buffer, report);
	fclose(report);

	launch_editor(report_path.buf, NULL, NULL);
	return 0;
}
