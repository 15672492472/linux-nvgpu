#!/usr/bin/perl -w
# Licensed under the terms of the GNU GPL License version 2
$P =~ s@(.*)/@@g;
my $D = $1;
my $ignore_changeid = 0;
my $max_line_length = 80;
  --max-line-length=n        set the maximum line length, if exceeded, warn
  --show-types               show the message "types" in the output
	if (!$ignore_perl_version) {
		exit(1);
	}
	print "$P: no input files\n";
	exit(1);
	if ($quiet == 0 && keys %$hashRef) {
		print "NOTE: $prefix message types:";
		print "\n\n";
			__init_refok|
			__rcu
			__bitwise__|
our $typeTypedefs = qr{(?x:
	printk(?:_ratelimited|_once|)|
	["(?:CLASS|DEVICE|SENSOR)_ATTR", 2],
my @spelling_list;
open(my $spelling, '<', $spelling_file)
    or die "$P: Can't open $spelling_file for reading: $!\n";
while (<$spelling>) {
	my $line = $_;
	$line =~ s/\s*\n?$//g;
	$line =~ s/^\s*//g;
	next if ($line =~ m/^\s*#/);
	next if ($line =~ m/^\s*$/);
	my ($suspect, $fix) = split(/\|\|/, $line);
	push(@spelling_list, $suspect);
	$spelling_fix{$suspect} = $fix;
close($spelling);
$misspellings = join("|", @spelling_list);
	my $mods = "(?x:  \n" . join("|\n  ", @modifierList) . "\n)";
	my $all = "(?x:  \n" . join("|\n  ", @typeList) . "\n)";
			(?:(?:\s|\*|\[\])+\s*const|(?:\s|\*\s*(?:const\s*)?|\[\])+|(?:\s*\[\s*\])+)?
			(?:(?:\s|\*|\[\])+\s*const|(?:\s|\*\s*(?:const\s*)?|\[\])+|(?:\s*\[\s*\])+)?
our $FuncArg = qr{$Typecast{0,1}($LvalOrFunc|$Constant)};
	(?:$Storage\s+)?(?:[A-Z_][A-Z0-9]*_){0,2}(?:DEFINE|DECLARE)(?:_[A-Z0-9]+){1,2}\s*\(|
	(?:$Storage\s+)?LIST_HEAD\s*\(|
	(?:$Storage\s+)?${Type}\s+uninitialized_var\s*\(
	if (-e ".git") {
		my $git_last_include_commit = `git log --no-merges --pretty=format:"%h%n" -1 -- include`;
	if (-e ".git") {
		$files = `git ls-files "include/*.h"`;
	return ($id, $desc) if ((which("git") eq "") || !(-e ".git"));
	my $output = `git log --no-color --format='%H %s' -1 $commit 2>&1`;
	if ($lines[0] =~ /^error: short SHA1 $commit is ambiguous\./) {
	if ($file) {
		$formatted_email =~ s/$address.*$//;
	return ($name, $address, $comment);
	my ($name, $address) = @_;
		$formatted_email = "$name <$address>";
	return $formatted_email;
			for (; ($n % 8) != 0; $n++) {
		# Comments we are wacking completly including the begin
	return "" if ($line !~ m/(\"[X]+\")/g);
	my ($current_comment) = ($rawlines[$end_line - 1] =~ m@.*(/\*.*\*/)\s*(?:\\\s*)?$@);
					push(@modifierList, $modifier);
			push(@typeList, $possible);
	my $line;
		$line = "$prefix$level:$type: $msg\n";
	} else {
		$line = "$prefix$level: $msg\n";
	$line = (split('\n', $line))[0] . "\n" if ($terse);
	push(our @report, $line);
		if ($line =~ /^(?:\+\+\+\|\-\-\-)\s+\S+/) {	#new filename
	my $source_indent = 8;

			if ($1 =~ m@Documentation/kernel-parameters.txt$@) {
		if ($rawline=~/^\@\@ -\d+(?:,\d+)? \+(\d+)(,(\d+))? \@\@/) {
		if ($line=~/^\@\@ -\d+(?:,\d+)? \+(\d+)(,(\d+))? \@\@/) {
#make up the handle for any error we report on this line
		$prefix = "$filename:$realline: " if ($emacs && $file);
		$prefix = "$filename:$linenr: " if ($emacs && !$file);

			if ($realfile =~ m@^(drivers/net/|net/)@) {
		if ($line =~ /^\s*signed-off-by:/i) {
			my ($email_name, $email_address, $comment) = parse_email($email);
			my $suggested_email = format_email(($email_name, $email_address));
				if ("$dequoted$comment" ne $email &&
				    "<$email_address>$comment" ne $email &&
				    "$suggested_email$comment" ne $email) {
					     "email address '$email' might be better as '$suggested_email$comment'\n" . $herecurr);
# Check for old stable address
		if ($line =~ /^\s*cc:\s*.*<?\bstable\@kernel\.org\b>?.*$/i) {
			ERROR("STABLE_ADDRESS",
			      "The 'stable' address should be 'stable\@vger.kernel.org'\n" . $herecurr);
# Check for unwanted Gerrit info
		if ($in_commit_log && !$ignore_changeid && $line =~ /^\s*change-id:/i) {
			      "Remove Gerrit Change-Id's before submitting upstream.\n" . $herecurr);
		}

# Check for improperly formed commit descriptions
		if ($in_commit_log &&
		    $line =~ /\bcommit\s+[0-9a-f]{5,}/i &&
		    !($line =~ /\b[Cc]ommit [0-9a-f]{12,40} \("/ ||
		      ($line =~ /\b[Cc]ommit [0-9a-f]{12,40}\s*$/ &&
		       defined $rawlines[$linenr] &&
		       $rawlines[$linenr] =~ /^\s*\("/))) {
			$line =~ /\b(c)ommit\s+([0-9a-f]{5,})/i;
			my $init_char = $1;
			my $orig_commit = lc($2);
			my $id = '01234567890ab';
			my $desc = 'commit description';
		        ($id, $desc) = git_commit_info($orig_commit, $id, $desc);
			ERROR("GIT_COMMIT_ID",
			      "Please use 12 or more chars for the git commit ID like: '${init_char}ommit $id (\"$desc\")'\n" . $herecurr);
			     "added, moved or deleted file(s), does MAINTAINERS need updating?\n" . $herecurr);
# Check for absolute kernel paths.
		if ($tree) {
			while ($line =~ m{(?:^|\s)(/\S*)}g) {
				my $file = $1;

				if ($file =~ m{^(.*?)(?::\d+)+:?$} &&
				    check_absolute_file($1, $herecurr)) {
					#
				} else {
					check_absolute_file($file, $herecurr);
				}
			}
		}

		    !($rawline =~ /^\s+\S/ ||
		      $rawline =~ /^(commit\b|from\b|[\w-]+:).*$/i)) {
		if ($in_commit_log || $line =~ /^\+/) {
			while ($rawline =~ /(?:^|[^a-z@])($misspellings)(?:$|[^a-z@])/gi) {
				my $msg_type = \&WARN;
				$msg_type = \&CHK if ($file);
				if (&{$msg_type}("TYPO_SPELLING",
						 "'$typo' may be misspelled - perhaps '$typo_fix'?\n" . $herecurr) &&
# ignore non-hunk lines and lines being removed
		next if (!$hunk_line || $line =~ /^-/);
			my $msg_type = \&ERROR;
			$msg_type = \&CHK if ($file);
			&{$msg_type}("FSF_MAILING_ADDRESS",
				     "Do not include the paragraph about writing to the Free Software Foundation's mailing address from the sample GPL notice. The FSF has changed addresses in the past, and may do so again. Linux already includes a copy of the GPL.\n" . $herevet)
		    $line =~ /^\+\s*config\s+/) {
				if ($lines[$ln - 1] =~ /^\+\s*(?:bool|tristate)\s*\"/) {
				if ($f =~ /^\s*config\s/) {
# discourage the addition of CONFIG_EXPERIMENTAL in Kconfig.
		    $line =~ /.\s*depends on\s+.*\bEXPERIMENTAL\b/) {
			WARN("CONFIG_EXPERIMENTAL",
			     "Use of CONFIG_EXPERIMENTAL is deprecated. For alternatives, see https://lkml.org/lkml/2012/10/23/580\n");
			my $vp_file = $dt_path . "vendor-prefixes.txt";
				`grep -Eq "^$vendor\\b" $vp_file`;
# check we are in a valid source file if not then ignore this hunk
		next if ($realfile !~ /\.(h|c|s|S|pl|sh|dtsi|dts)$/);

#line length limit
		if ($line =~ /^\+/ && $prevrawline !~ /\/\*\*/ &&
		    $rawline !~ /^.\s*\*\s*\@$Ident\s/ &&
		    !($line =~ /^\+\s*$logFunctions\s*\(\s*(?:(KERN_\S+\s*|[^"]*))?"[X\t]*"\s*(?:|,|\)\s*;)\s*$/ ||
		    $line =~ /^\+\s*"[^"]*"\s*(?:\s*|,|\)\s*;)\s*$/) &&
		    $length > $max_line_length)
		{
			WARN("LONG_LINE",
			     "line over $max_line_length characters\n" . $herecurr);
# Check for user-visible strings broken across lines, which breaks the ability
# to grep for the string.  Make exceptions when the previous string ends in a
# newline (multiple lines in one string constant) or '\t', '\r', ';', or '{'
# (common in inline assembly) or is a octal \123 or hexadecimal \xaf value
		if ($line =~ /^\+\s*"/ &&
		    $prevline =~ /"\s*$/ &&
		    $prevrawline !~ /(?:\\(?:[ntr]|[0-7]{1,3}|x[0-9a-fA-F]{1,2})|;\s*|\{\s*)"\s*$/) {
			WARN("SPLIT_STRING",
			     "quoted string split across lines\n" . $hereprev);
# check for missing a space in a string concatination
		if ($prevrawline =~ /[^\\]\w"$/ && $rawline =~ /^\+[\t ]+"\w/) {
			WARN('MISSING_SPACE',
			     "break quoted strings at a space character\n" . $hereprev);
# check for spaces before a quoted newline
		if ($rawline =~ /^.*\".*\s\\n/) {
			if (WARN("QUOTED_WHITESPACE_BEFORE_NEWLINE",
				 "unnecessary whitespace before a quoted newline\n" . $herecurr) &&
			    $fix) {
				$fixed[$fixlinenr] =~ s/^(\+.*\".*)\s+\\n/$1\\n/;
# Blackfin: use hi/lo macros
		if ($realfile =~ m@arch/blackfin/.*\.S$@) {
			if ($line =~ /\.[lL][[:space:]]*=.*&[[:space:]]*0x[fF][fF][fF][fF]/) {
				my $herevet = "$here\n" . cat_vet($line) . "\n";
				ERROR("LO_MACRO",
				      "use the LO() macro, not (... & 0xFFFF)\n" . $herevet);
			}
			if ($line =~ /\.[hH][[:space:]]*=.*>>[[:space:]]*16/) {
				my $herevet = "$here\n" . cat_vet($line) . "\n";
				ERROR("HI_MACRO",
				      "use the HI() macro, not (... >> 16)\n" . $herevet);
			}
		}

# more than 8 must use tabs.
					   s/(^\+.*) {8,8}\t/$1\t\t/) {}
		if ($^V && $^V ge 5.10.0 &&
		    $prevline =~ /^\+([ \t]*)((?:$c90_Keywords(?:\s+if)\s*)|(?:$Declare\s*)?(?:$Ident|\(\s*\*\s*$Ident\s*\))\s*|$Ident\s*=\s*$Ident\s*)\(.*(\&\&|\|\||,)\s*$/) {
					"\t" x ($pos / 8) .
					" "  x ($pos % 8);
		if ($line =~ /^\+.*\(\s*$Type\s*\)[ \t]+(?!$Assignment|$Arithmetic|{)/) {
		    $realline > 2) {
		if ($realfile =~ m@^(drivers/net/|net/)@ &&
		    $prevrawline =~ /^\+[ \t]*\/\*/ &&		#starting /*
			WARN("NETWORKING_BLOCK_COMMENT_STYLE",
			     "networking block comments start with * on subsequent lines\n" . $hereprev);
		if ($realfile =~ m@^(drivers/net/|net/)@ &&
		    $rawline !~ m@^\+[ \t]*\*/[ \t]*$@ &&	#trailing */
			WARN("NETWORKING_BLOCK_COMMENT_STYLE",
			     "networking block comments put the trailing */ on a separate line\n" . $herecurr);
		      $sline =~ /^\+\s+(?:union|struct|enum|typedef)\b/ ||
# discourage the addition of CONFIG_EXPERIMENTAL in #if(def).
		if ($line =~ /^\+\s*\#\s*if.*\bCONFIG_EXPERIMENTAL\b/) {
			WARN("CONFIG_EXPERIMENTAL",
			     "Use of CONFIG_EXPERIMENTAL is deprecated. For alternatives, see https://lkml.org/lkml/2012/10/23/580\n");
		}

# Blackfin: don't use __builtin_bfin_[cs]sync
		if ($line =~ /__builtin_bfin_csync/) {
			my $herevet = "$here\n" . cat_vet($line) . "\n";
			ERROR("CSYNC",
			      "use the CSYNC() macro in asm/blackfin.h\n" . $herevet);
		}
		if ($line =~ /__builtin_bfin_ssync/) {
			my $herevet = "$here\n" . cat_vet($line) . "\n";
			ERROR("SSYNC",
			      "use the SSYNC() macro in asm/blackfin.h\n" . $herevet);
		}

		if ($linenr >= $suppress_statement &&
		if ($line =~ /\b(?:(?:if|while|for|(?:[a-z_]+|)for_each[a-z_]+)\s*\(|do\b)/ && $line !~ /^.\s*#/ && $line !~ /\}\s*while\s*/) {

			# Find out how long the conditional actually is.
			my @newlines = ($c =~ /\n/gs);
			my $cond_lines = 1 + $#newlines;
			if ($check && (($sindent % 8) != 0 ||
			    ($sindent <= $indent && $s ne ''))) {
				$fixedline =~ s/^(.\s*){\s*/$1/;
		if ($line =~ /^\+(\s*$Type\s*$Ident\s*(?:\s+$Modifier))*\s*=\s*(0|NULL|false)\s*;/) {
				  "do not initialise globals to 0 or NULL\n" .
				      $herecurr) &&
				$fixed[$fixlinenr] =~ s/($Type\s*$Ident\s*(?:\s+$Modifier))*\s*=\s*(0|NULL|false)\s*;/$1;/;
		if ($line =~ /^\+.*\bstatic\s.*=\s*(0|NULL|false)\s*;/) {
				  "do not initialise statics to 0 or NULL\n" .
				$fixed[$fixlinenr] =~ s/(\bstatic\s.*?)\s*=\s*(0|NULL|false)\s*;/$1;/;
               }
               }
		if ($line =~ /(\b$Type\s+$Ident)\s*\(\s*\)/) {
# check for uses of DEFINE_PCI_DEVICE_TABLE
		if ($line =~ /\bDEFINE_PCI_DEVICE_TABLE\s*\(\s*(\w+)\s*\)\s*=/) {
			if (WARN("DEFINE_PCI_DEVICE_TABLE",
				 "Prefer struct pci_device_id over deprecated DEFINE_PCI_DEVICE_TABLE\n" . $herecurr) &&
			    $fix) {
				$fixed[$fixlinenr] =~ s/\b(?:static\s+|)DEFINE_PCI_DEVICE_TABLE\s*\(\s*(\w+)\s*\)\s*=\s*/static const struct pci_device_id $1\[\] = /;
			}
		}

		    $line !~ /\b__bitwise(?:__|)\b/) {
# # no BUG() or BUG_ON()
# 		if ($line =~ /\b(BUG|BUG_ON)\b/) {
# 			print "Try to use WARN_ON & Recovery code rather than BUG() or BUG_ON()\n";
# 			print "$herecurr";
# 			$clean = 0;
# 		}
"Prefer printk_ratelimited or pr_<level>_ratelimited to printk_ratelimit\n" . $herecurr);
# printk should use KERN_* levels.  Note that follow on printk's on the
# same line do not need a level, so we use the current block context
# to try and find and validate the current printk.  In summary the current
# printk includes all preceding printk's which have no newline on the end.
# we assume the first bad printk is the one to report.
		if ($line =~ /\bprintk\((?!KERN_)\s*"/) {
			my $ok = 0;
			for (my $ln = $linenr - 1; $ln >= $first_line; $ln--) {
				#print "CHECK<$lines[$ln - 1]\n";
				# we have a preceding printk if it ends
				# with "\n" ignore it, else it is to blame
				if ($lines[$ln - 1] =~ m{\bprintk\(}) {
					if ($rawlines[$ln - 1] !~ m{\\n"}) {
						$ok = 1;
					}
					last;
				}
			}
			if ($ok == 0) {
				WARN("PRINTK_WITHOUT_KERN_LEVEL",
				     "printk() should include KERN_ facility level\n" . $herecurr);
			}
		if ($line =~ /\bpr_warning\s*\(/) {
			if (WARN("PREFER_PR_LEVEL",
				 "Prefer pr_warn(... to pr_warning(...\n" . $herecurr) &&
			    $fix) {
				$fixed[$fixlinenr] =~
				    s/\bpr_warning\b/pr_warn/;
			}
		}

		if (($line=~/$Type\s*$Ident\(.*\).*\s*{/) and
		    !($line=~/\#\s*define.*do\s{/) and !($line=~/}/)) {
				  "open brace '{' following function declarations go on the next line\n" . $herecurr) &&
				$fixedline =~ s/^(.\s*){\s*/$1\t/;
			    $prefix !~ /[{,]\s+$/) {
				    $ca =~ /\s$/ && $cc =~ /^\s*,/) {
				# , must have a space on the right.
							$good = $fix_elements[$n] . trim($fix_elements[$n + 1]) . " ";
					if ($ctx =~ /Wx[^WCE]|[^WCE]xW/) {
					    	$ok = 1;
						my $msg_type = \&ERROR;
						$msg_type = \&CHK if (($op eq '?:' || $op eq '?' || $op eq ':') && $ctx =~ /VxV/);
						if (&{$msg_type}("SPACING",
								 "spaces required around that '$op' $at\n" . $hereptr)) {
		if (($line =~ /\(.*\){/ && $line !~ /\($Type\){/) ||
		    $line =~ /do{/) {
				$fixed[$fixlinenr] =~ s/^(\+.*(?:do|\))){/$1 {/;
		if ($line =~ /}(?!(?:,|;|\)))\S/) {
			CHK("UNNECESSARY_PARENTHESES",
			    "Unnecessary parentheses around $1\n" . $herecurr);
		    }
			if ($^V && $^V ge 5.10.0 &&
		if ($^V && $^V ge 5.10.0 &&
# Return of what appears to be an errno should normally be -'ve
		if ($line =~ /^.\s*return\s*(E[A-Z]*)\s*;/) {
				     "return of an errno should typically be -ve (return -$1)\n" . $herecurr);
				ERROR("ASSIGN_IN_IF",
				      "do not use assignment in if condition\n" . $herecurr);
			$s =~ s/$;//g; 	# Remove any comments
			$s =~ s/$;//g; 	# Remove any comments
#gcc binary extension
			if ($var =~ /^$Binary$/) {
				if (WARN("GCC_BINARY_CONSTANT",
					 "Avoid gcc v4.3+ binary constant extension: <$var>\n" . $herecurr) &&
				    $fix) {
					my $hexval = sprintf("0x%x", oct($var));
					$fixed[$fixlinenr] =~
					    s/\b$var\b/$hexval/;
				}
			}

#Ignore SI style variants like nS, mV and dB (ie: max_uV, regulator_min_uA_show)
			    $var !~ /^(?:[a-z_]*?)_?[a-z][A-Z](?:_[a-z_]+)?$/) {
#warn if <asm/foo.h> is #included and <linux/foo.h> is available (uses RAW line)
				if ($realfile =~ m{^arch/}) {
					CHK("ARCH_INCLUDE_LINUX",
					    "Consider using #include <linux/$file> instead of <asm/$file>\n" . $herecurr);
				} else {
					WARN("INCLUDE_LINUX",
					     "Use #include <linux/$file> instead of <asm/$file>\n" . $herecurr);
			$has_arg_concat = 1 if ($ctx =~ /\#\#/);
			$dstat =~ s/^.\s*\#\s*define\s+$Ident(?:\([^\)]*\))?\s*//;
			while ($dstat =~ s/\([^\(\)]*\)/1/ ||
			       $dstat =~ s/\{[^\{\}]*\}/1/ ||
			       $dstat =~ s/\[[^\[\]]*\]/1/)
			# Flatten any obvious string concatentation.
			while ($dstat =~ s/("X*")\s*$Ident/$1/ ||
			       $dstat =~ s/$Ident\s*("X*")/$1/)
				^\"|\"$
			    $dstat !~ /^\({/ &&						# ({...
				$ctx =~ s/\n*$//;
				my $herectx = $here . "\n";
				my $cnt = statement_rawlines($ctx);

				for (my $n = 0; $n < $cnt; $n++) {
					$herectx .= raw_line($linenr, $n) . "\n";
				}

				if ($dstat =~ /;/) {
				my $herectx = $here . "\n";
				for (my $n = 0; $n < $cnt; $n++) {
					$herectx .= raw_line($linenr, $n) . "\n";
				}
		if ($^V && $^V ge 5.10.0 &&
				my $herectx = $here . "\n";

				for (my $n = 0; $n < $cnt; $n++) {
					$herectx .= raw_line($linenr, $n) . "\n";
				}
				my $herectx = $here . "\n";

				for (my $n = 0; $n < $cnt; $n++) {
					$herectx .= raw_line($linenr, $n) . "\n";
				}
# make sure symbols are always wrapped with VMLINUX_SYMBOL() ...
# all assignments may have only one of the following with an assignment:
#	.
#	ALIGN(...)
#	VMLINUX_SYMBOL(...)
		if ($realfile eq 'vmlinux.lds.h' && $line =~ /(?:(?:^|\s)$Ident\s*=|=\s*$Ident(?:\s|$))/) {
			WARN("MISSING_VMLINUX_SYMBOL",
			     "vmlinux.lds.h needs VMLINUX_SYMBOL() around C-visible symbols\n" . $herecurr);
		}

				my $herectx = $here . "\n";

				for (my $n = 0; $n < $cnt; $n++) {
					$herectx .= raw_line($linenr, $n) . "\n";
				}
			CHK("BRACES",
			    "Blank lines aren't necessary before a close brace '}'\n" . $hereprev);
			CHK("BRACES",
			    "Blank lines aren't necessary after an open brace '{'\n" . $hereprev);
			     "Use of volatile is usually wrong: see Documentation/volatile-considered-harmful.txt\n" . $herecurr);
		if ($line =~ /"X+"[A-Z_]+/ || $line =~ /[A-Z_]+"X+"/) {
			CHK("CONCATENATED_STRING",
			    "Concatenated strings should use spaces between elements\n" . $herecurr);
			CHK("REDUNDANT_CODE",
			    "if this code is redundant consider removing it\n" .
				$herecurr);
			my $expr = '\s*\(\s*' . quotemeta($1) . '\s*\)\s*;';
			if ($line =~ /\b(kfree|usb_free_urb|debugfs_remove(?:_recursive)?)$expr/) {
				WARN('NEEDLESS_IF',
				     "$1(NULL) is safe this check is probably not required\n" . $hereprev);
			if ($c =~ /(?:^|\n)[ \+]\s*(?:$Type\s*)?\Q$testval\E\s*=\s*(?:\([^\)]*\)\s*)?\s*(?:devm_)?(?:[kv][czm]alloc(?:_node|_array)?\b|kstrdup|(?:dev_)?alloc_skb)/) {
		if ($line !~ /printk\s*\(/ &&
				    "usleep_range is preferred over udelay; see Documentation/timers/timers-howto.txt\n" . $herecurr);
				     "msleep < 20ms can sleep for up to 20ms; see Documentation/timers/timers-howto.txt\n" . $herecurr);
		if ($line =~ /\b(mb|rmb|wmb|read_barrier_depends|smp_mb|smp_rmb|smp_wmb|smp_read_barrier_depends)\(/) {
		if ($line =~ /\b$Storage\b/ && $line !~ /^.\s*$Storage\b/) {
			     "storage class should be at the beginning of the declaration\n" . $herecurr)
# check for line continuations in quoted strings with odd counts of "
		if ($rawline =~ /\\$/ && $rawline =~ tr/"/"/ % 2) {
			WARN("LINE_CONTINUATIONS",
			     "Avoid line continuations in quoted strings\n" . $herecurr);
		}

			if ($fmt ne "" && $fmt !~ /[^\\]\%/) {
		if ($^V && $^V ge 5.10.0 &&
		    $stat =~ /^\+(?:.*?)\bmemset\s*\(\s*$FuncArg\s*,\s*$FuncArg\s*\,\s*$FuncArg\s*\)/s) {
		if ($^V && $^V ge 5.10.0 &&
		    $line =~ /^\+(?:.*?)\bmemcpy\s*\(\s*$FuncArg\s*,\s*$FuncArg\s*\,\s*ETH_ALEN\s*\)/s) {
			if (WARN("PREFER_ETHER_ADDR_COPY",
				 "Prefer ether_addr_copy() over memcpy() if the Ethernet addresses are __aligned(2)\n" . $herecurr) &&
			    $fix) {
				$fixed[$fixlinenr] =~ s/\bmemcpy\s*\(\s*$FuncArg\s*,\s*$FuncArg\s*\,\s*ETH_ALEN\s*\)/ether_addr_copy($2, $7)/;
			}
		}
		if ($^V && $^V ge 5.10.0 &&
		if ($^V && $^V ge 5.10.0 &&
				     "usleep_range should not use min == max args; see Documentation/timers/timers-howto.txt\n" . "$here\n$stat\n");
				     "usleep_range args reversed, use min then max; see Documentation/timers/timers-howto.txt\n" . "$here\n$stat\n");
		if ($^V && $^V ge 5.10.0 &&
			my $stat_real = raw_line($linenr, 0);
		        for (my $count = $linenr + 1; $count <= $lc; $count++) {
				$stat_real = $stat_real . "\n" . raw_line($count, 0);
			}
		if ($^V && $^V ge 5.10.0 &&
			my $stat_real = raw_line($linenr, 0);
		        for (my $count = $linenr + 1; $count <= $lc; $count++) {
				$stat_real = $stat_real . "\n" . raw_line($count, 0);
			}
			if ($s =~ /^\s*;/ &&
			    $function_name ne 'uninitialized_var')
				    "__setup appears un-documented -- check Documentation/kernel-parameters.txt\n" . $herecurr);
# check for pointless casting of kmalloc return
		if ($line =~ /\*\s*\)\s*[kv][czm]alloc(_node){0,1}\b/) {
		if ($^V && $^V ge 5.10.0 &&
		    $line =~ /\b($Lval)\s*\=\s*(?:$balanced_parens)?\s*([kv][mz]alloc(?:_node)?)\s*\(\s*(sizeof\s*\(\s*struct\s+$Lval\s*\))/) {
		if ($^V && $^V ge 5.10.0 &&
		    $line =~ /\b($Lval)\s*\=\s*(?:$balanced_parens)?\s*(k[mz]alloc)\s*\(\s*($FuncArg)\s*\*\s*($FuncArg)\s*,/) {
					 "Prefer $newfunc over $oldfunc with multiply\n" . $herecurr) &&

		if ($^V && $^V ge 5.10.0 &&
		    $line =~ /\b($Lval)\s*\=\s*(?:$balanced_parens)?\s*krealloc\s*\(\s*\1\s*,/) {
# check for case / default statements not preceded by break/fallthrough/switch
		if ($line =~ /^.\s*(?:case\s+(?:$Ident|$Constant)\s*|default):/) {
			my $has_break = 0;
			my $has_statement = 0;
			my $count = 0;
			my $prevline = $linenr;
			while ($prevline > 1 && ($file || $count < 3) && !$has_break) {
				$prevline--;
				my $rline = $rawlines[$prevline - 1];
				my $fline = $lines[$prevline - 1];
				last if ($fline =~ /^\@\@/);
				next if ($fline =~ /^\-/);
				next if ($fline =~ /^.(?:\s*(?:case\s+(?:$Ident|$Constant)[\s$;]*|default):[\s$;]*)*$/);
				$has_break = 1 if ($rline =~ /fall[\s_-]*(through|thru)/i);
				next if ($fline =~ /^.[\s$;]*$/);
				$has_statement = 1;
				$count++;
				$has_break = 1 if ($fline =~ /\bswitch\b|\b(?:break\s*;[\s$;]*$|return\b|goto\b|continue\b)/);
			if (!$has_break && $has_statement) {
				WARN("MISSING_BREAK",
				     "Possible switch case/default not preceeded by break or fallthrough comment\n" . $herecurr);
		if ($^V && $^V ge 5.10.0 &&
			my $ctx = '';
			my $herectx = $here . "\n";
			for (my $n = 0; $n < $cnt; $n++) {
				$herectx .= raw_line($linenr, $n) . "\n";
			}
# check for various ops structs, ensure they are const.
		my $struct_ops = qr{acpi_dock_ops|
				address_space_operations|
				backlight_ops|
				block_device_operations|
				dentry_operations|
				dev_pm_ops|
				dma_map_ops|
				extent_io_ops|
				file_lock_operations|
				file_operations|
				hv_ops|
				ide_dma_ops|
				intel_dvo_dev_ops|
				item_operations|
				iwl_ops|
				kgdb_arch|
				kgdb_io|
				kset_uevent_ops|
				lock_manager_operations|
				microcode_ops|
				mtrr_ops|
				neigh_ops|
				nlmsvc_binding|
				pci_raw_ops|
				pipe_buf_operations|
				platform_hibernation_ops|
				platform_suspend_ops|
				proto_ops|
				rpc_pipe_ops|
				seq_operations|
				snd_ac97_build_ops|
				soc_pcmcia_socket_ops|
				stacktrace_ops|
				sysfs_ops|
				tty_operations|
				usb_mon_operations|
				wd_ops}x;
		if ($line !~ /\bconst\b/ &&
		    $line =~ /\bstruct\s+($struct_ops)\b/) {
			     "struct $1 should normally be const\n" .
				$herecurr);
# check for %L{u,d,i} in strings
		my $string;
		while ($line =~ /(?:^|")([X\t]*)(?:"|$)/g) {
			$string = substr($rawline, $-[1], $+[1] - $-[1]);
			$string =~ s/%%/__/g;
			if ($string =~ /(?<!%)%L[udi]/) {
				WARN("PRINTF_L",
				     "\%Ld/%Lu are not-standard C, use %lld/%llu\n" . $herecurr);
				last;
			}
		if ($line =~ /debugfs_create_file.*S_IWUGO/ ||
		    $line =~ /DEVICE_ATTR.*S_IWUGO/ ) {
		if ($^V && $^V ge 5.10.0 &&
				my $test = "\\b$func\\s*\\(${skip_args}([\\d]+)\\s*[,\\)]";
				if ($line =~ /$test/) {

					if ($val !~ /^0$/ &&
					     length($val) ne 4)) {
						      "Use 4 digit octal (0777) not decimal permissions\n" . $herecurr);
	if (!$is_patch) {
	if ($is_patch && $chk_signoff && $signoff == 0) {
		ERROR("MISSING_SIGN_OFF",
		      "Missing Signed-off-by: line(s)\n");
		print "\n" if ($quiet == 0);
		if ($^V lt 5.10.0) {
			print("NOTE: perl $^V is not modern enough to detect all possible issues.\n");
			print("An upgrade to at least perl v5.10.0 is suggested.\n\n");

			print "NOTE: whitespace errors detected, you may wish to use scripts/cleanpatch or\n";
			print "      scripts/cleanfile\n\n";
	hash_show_words(\%use_type, "Used");
	hash_show_words(\%ignore_type, "Ignored");


	if ($clean == 1 && $quiet == 0) {
		print "$vname has no obvious style problems and is ready for submission.\n"
	if ($clean == 0 && $quiet == 0) {
		print << "EOM";
$vname has style problems, please review.

If any of these errors are false positives, please report
them to the maintainer, see CHECKPATCH in MAINTAINERS.
EOM
	}
