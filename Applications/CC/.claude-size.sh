#!/bin/sh
# Temporary review script: where the bytes in the board bcrun go.
cd /home/peter/src/FUZIX/Applications/CC || exit 1
echo "== size =="
size bcrun
echo "== totals by family (text+data, bytes) =="
nm --print-size --radix=d bcrun | awk '
  $3 ~ /^[tTdDrR]$/ {
    name=$4; sz=$2+0
    if (name ~ /^mm_ls_/) f="LONGSTRING (mm_ls_*)"
    else if (name ~ /^mm_(sort|arr|st)_/) f="sort/array/stats"
    else if (name ~ /^mm_(fb|pixel|cls|line|plot|fill|map|colour|mode|hres|vres|fg|bg|gtext|font|gputc|gflush|g[a-z]*)/) f="graphics+console-gfx"
    else if (name ~ /^mm_(play|snd)_/) f="sound"
    else if (name ~ /^mm_(i2c|spi|gpio|rtcreg|pinno|keydown)/) f="hw buses/gpio"
    else if (name ~ /^mm_(open|close|flush|fpr|eof|loc|lof|seek|getline|input|kill|rename|copy|mkdir|rmdir|chdir|cwd|dir|files|exists|filesize)/) f="BASIC file layer"
    else if (name ~ /^mm_(data|read|restore)/) f="DATA/READ"
    else if (name ~ /^mm_(epoch|datetime|time_str|date_str|day|set_date|set_time|timer|us|pause|break_epoch)/) f="date/time"
    else if (name ~ /^mm_(int_to_str|float_to_str|str_f|str_i|hex|oct|bin|bin2str|str2bin|format|fmt)/) f="number formatting"
    else if (name ~ /^mm_(sset|scat|scmp|scopy|left|right|mid|ucase|lcase|ltrim|rtrim|space|strrep|asc|instr|val|chr|byte|trim|field|tmp|mark|release|byref)/) f="BASIC strings/scratch"
    else if (name ~ /^(mm_|mmrt_|w_)/ || name == "mmwtab" ) f="mm other+wrappers"
    else if (name ~ /^(bc_exec|helper_|native_|libcall|lib_resolve|exec_eqop|parse_eqop|call_target)/) f="interpreter core"
    else if (name ~ /^(do_format|fmt_double|fmt64|padout|emit|getstr|out_flush|_fnum|fnum_digits|_vfnprintf)/) f="printf/format (C)"
    else if (name ~ /^(lib_|lc_|ns_|vcopy|vstrlen|heap_|psram)/) f="C libcalls+heap"
    else if (name ~ /^__aeabi|^__[a-z]*(df|sf|di|si)|^__udivmod|^__muldi/) f="gcc soft-fp/int"
    else f="libc/other"
    tot[f]+=sz; n[f]++
  }
  END { for (k in tot) printf "%9d  %4d  %s\n", tot[k], n[k], k }' | sort -rn
echo "== biggest mm_* symbols =="
nm --print-size --size-sort --radix=d bcrun | awk '$4 ~ /^(mm|w_|mmrt|mmw)/ {print $2, $4}' | sort -rn | head -40
echo "== total mm-family =="
nm --print-size --radix=d bcrun | awk '$3 ~ /^[tTdDrR]$/ && ($4 ~ /^(mm_|mmrt_|w_)/ || $4=="mmwtab") {s+=$2} END {print s " bytes"}'
