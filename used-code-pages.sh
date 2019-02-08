#! /bin/bash
fname="$1"
gawk '$3 == "C" { cnt[strtonum($2)]=gensub(/^\(?([^:)]*).*/,"\\1","g",$NF) } END { PROCINFO["sorted_in"]= "@ind_num_asc"; start=-1; last=-1; for (l in cnt) if (last+4096==l) last=l; else { if (last!=-1) printf("0x%x-0x%x %s\n", start,last+4095,cnt[l]); start=l; last=l; } }' < "$fname"
