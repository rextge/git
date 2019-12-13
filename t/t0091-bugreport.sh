#!/bin/bash

test_description='git bugreport'

. ./test-lib.sh

# Headers "[System Info]" will be followed by a non-empty line if we put some
# information there; we can make sure all our headers were followed by some
# information to check if the command was successful.
HEADER_PATTERN="^\[.*\]$"
check_all_headers_populated() {
	while read -r line; do
		if [$(grep $HEADER_PATTERN $line)]; then
			read -r nextline
			if [-z $nextline]; then
				return 1;
			fi
		fi
	done
}

test_expect_success 'creates a report with content in the right places' '
	git bugreport &&
	check_all_headers_populated <git-bugreport-* &&
	rm git-bugreport-*
'

test_expect_success '--output puts the report in the provided dir' '
	mkdir foo/ &&
	git bugreport -o foo/ &&
	test -f foo/git-bugreport-* &&
	rm -fr foo/
'

test_expect_success 'incorrect arguments abort with usage' '
	test_must_fail git bugreport --false 2>output &&
	grep usage output &&
	test ! -f git-bugreport-*
'

test_done
