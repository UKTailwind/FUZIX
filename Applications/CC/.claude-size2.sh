#!/bin/sh
cd /home/peter/src/FUZIX/Applications/CC || exit 1
echo "== wrapper layer (w_* text) =="
nm --print-size --radix=d bcrun | awk '$3=="t" && $4 ~ /^w_/ {s+=$2; n++} END {print s" bytes in "n" wrappers"}'
echo "== mmwtab + lookup =="
nm --print-size --radix=d bcrun | awk '$4=="mmwtab"||$4=="mm_wrap_lookup" {print $2, $4}'
echo "== mm_* text total (runtime bodies, no wrappers) =="
nm --print-size --radix=d bcrun | awk '$3 ~ /^[tT]$/ && $4 ~ /^mm_/ {s+=$2; n++} END {print s" bytes in "n" functions"}'
echo "== mm-family bss (RAM per process) =="
nm --print-size --radix=d bcrun | awk '$3 ~ /^[bB]$/ && $4 ~ /^mm/ {s+=$2; n++; if ($2+0>=200) print $2, $4} END {print "total", s" bytes in "n" objects"}'
echo "== families, text only =="
nm --print-size --radix=d bcrun | awk '
  $3 ~ /^[tT]$/ {
    name=$4; sz=$2+0
    if (name ~ /^mm_ls_/) f="LONGSTRING"
    else if (name ~ /^mm_(sort|seed_index|sort_range|scmp_ci|sort_cmp)/) f="SORT"
    else if (name ~ /^mm_(st_|kth_|arr_)/) f="array+stats"
    else if (name ~ /^mm_(data|read_|restore|next_idx|d4_)/) f="DATA/READ"
    else if (name ~ /^mm_(civil|break_epoch|days_from|parse_hms|epoch|datetime|time_str|date_str|day|set_date|set_time)/) f="date/time"
    else if (name ~ /^mm_(int_to_str|float_to_str|str_f|str_i|base|hex|oct|bin2str|str2bin|format|fmt_exp|decexp|striptz|bin$|oct$)/) f="num-format"
    else if (name ~ /^mm_(sset|scat|scmp$|scopy|slen|cstr|left|right|mid|ucase$|lcase$|ltrim|rtrim|space|strrep|asc|instr$|val|chr|byte$|trim|field|scan_delim|in_mask|mid_assign|tmp|mark|release|byref)/) f="strings+scratch"
    else if (name ~ /^mm_(toint|idiv|mod|fdiv|pow|sqr|log|asin|acos|atan3|rnd|randomize|rng|seeded|sgn|int$|fix|bit_|byte_a|flag|flags)/) f="numeric"
    else if (name ~ /^mm_(open|close|flush|ch$|outc|fpr|eof|loc|lof|seek|getc|readline|getline|input|atoi|atof|kill|rename|copy|mkdir|rmdir|chdir|cwd|dir|files|wild|exists|filesize|ls_file)/) f="file-layer"
    else if (name ~ /^mm_(gputc|gflush|gtext|font|gfx|pix|plot|fill$|line|pixel|cls|map|colour|mode|hres|vres|fb_|at$|con_mirror|board_no|hpos|vpos|col$|charpos|tab$|iodrain|iotrace|txtcopy|scroll)/) f="graphics"
    else if (name ~ /^mm_(putc|pr_|puts_raw|console)/) f="console-print"
    else if (name ~ /^mm_(run_|play_|snd_|pipe)/) f="run/exec/sound"
    else if (name ~ /^mm_(inkey|key_|kpush|rd1|esc_decode|raw_|keydown)/) f="inkey/keys"
    else if (name ~ /^mm_(error|fatal|err_bind|on_error|int_err|armed|arming|poisoned|ssink|fsink|isink|errno|errmsg)/) f="error-machinery"
    else if (name ~ /^mm_(i2c|spi|rtcreg|gpio|pinno)/) f="i2c/spi/gpio"
    else if (name ~ /^mm_(heap|lheap|lfree)/) f="dead-hosted-heap"
    else if (name ~ /^mm_(ver|device|platform|path|current|drive|cmdline|argv_bind|dev_name|us|us_now|timer|pause|end$|gosub|tb|tb_hex|tb_on|clock_offset|i2c_setstat)/) f="misc-info"
    else if (name ~ /^(mm_|mmrt_)/) f="mm-UNCLASSIFIED"
    else next
    tot[f]+=sz; n[f]++
  }
  END { for (k in tot) printf "%7d %4d  %s\n", tot[k], n[k], k }' | sort -rn
echo "== unclassified names =="
nm --print-size --radix=d bcrun | awk '$3 ~ /^[tT]$/ && $4 ~ /^mm/' | awk '
  $4 !~ /^mm_(ls_|sort|seed_index|sort_range|scmp_ci|sort_cmp|st_|kth_|arr_|data|read_|restore|next_idx|d4_|civil|break_epoch|days_from|parse_hms|epoch|datetime|time_str|date_str|day|set_date|set_time|int_to_str|float_to_str|str_f|str_i|base|hex|oct|bin2str|str2bin|format|fmt_exp|decexp|striptz|sset|scat|scopy|slen|cstr|left|right|mid|ltrim|rtrim|space|strrep|asc|val|chr|trim|field|scan_delim|in_mask|tmp|mark|release|byref|toint|idiv|mod|fdiv|pow|sqr|log|asin|acos|atan3|rnd|randomize|rng|seeded|sgn|fix|bit_|byte_a|flag|flags|open|close|flush|outc|fpr|eof|loc|lof|seek|getc|readline|getline|input|atoi|atof|kill|rename|copy|mkdir|rmdir|chdir|cwd|dir|files|wild|exists|filesize|ls_file|gputc|gflush|gtext|font|gfx|pix|plot|line|pixel|cls|map|colour|mode|hres|vres|fb_|con_mirror|board_no|hpos|vpos|iodrain|iotrace|txtcopy|scroll|putc|pr_|puts_raw|console|run_|play_|snd_|pipe|inkey|key_|kpush|rd1|esc_decode|raw_|keydown|error|fatal|err_bind|on_error|int_err|armed|arming|poisoned|ssink|fsink|isink|errno|errmsg|i2c|spi|rtcreg|gpio|pinno|heap|lheap|lfree|ver|device|platform|path|current|drive|cmdline|argv_bind|dev_name|us|us_now|timer|pause|end|gosub|tb|clock_offset)/ {print $2, $4}'
