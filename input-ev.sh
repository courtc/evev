#!/usr/bin/bash
# SPDX-License-Identifier: BSD-2-Clause

export LC_COLLATE=C
input_event_codes="$1"
declare -A evtab
declare -A nametabs
table=""

parse_codes()
{
	sed '/\#define\s\+[A-Z0-9_]\+\s\+[0-9xA-Fa-f]\+/s/\#define\s\+\(\([A-Z0-9]\+\)_[A-Z0-9_]\+\)\s\+\([0-9xA-Fa-f]\+\)\(\s.*\)\?$/\2 \3 \1/p;d' "$1"
}

codes_to_nametabs()
{
	parse_codes "$1" |
	while read ctab code name; do
		if [ x"$ctab" == x"BTN" ]; then
			ctab="KEY"
		fi
		if [ x"${name:(-4)}" == x"_MAX" ]; then
			continue
		fi
		printf "%s %d %s\n" "$ctab" "$code" "$name"
	done
}

codes_to_codetab()
{
	parse_codes "$1" |
	while read ctab code name; do
		if [ x"$ctab" == x"BTN" ]; then
			ctab="KEY"
		fi
		if [ x"${name:(-4)}" == x"_MAX" ]; then
			continue
		fi
		printf "%s %s %d\n" "$name" "$ctab" "$code"
	done
}

echo "#include \"tables.h\""
echo "#include \"types.h\""
echo

while read ctab code name; do
	if [ x"$ctab" != x"$table" ]; then
		if [ x"$table" != x"" ]; then
			echo "};"
			echo
		fi
		table="$ctab"
		echo "static const char *nametab_$ctab[] = {"
		nametabs[$ctab]="$ctab"
	fi
	if [ x"$ctab" == x"EV" ]; then
		evtab["${name:3}"]="$code"
	fi
	printf "\t[%# 5x] = \"%s\",\n" "$code" "$name"
	
done << EOS
$(codes_to_nametabs $input_event_codes | sort -V)
EOS
echo "};"
echo

echo "const struct code_entry codetab[] = {"
while read name ctab code; do
	printf "\t{ %-30s %#x, %#x },\n" "\"$name\"," "${evtab[$ctab]}" "$code"
done << EOS
$(codes_to_codetab $input_event_codes | sort -nk 1.1,2.1)
EOS
echo "};"
echo "const unsigned int codetab_sz = ARRAY_SIZE(codetab);"
echo

echo "const struct nametab_entry nametab[] = {"
for name in ${!evtab[@]}; do
	code=${evtab[$name]}
	if [ x"${nametabs[$name]}" == x"" ]; then
		continue
	fi
	printf "\t[%# 5x] = { \"%s\", nametab_%s },\n" "$code" "$name" "$name"
done
echo "};"
echo "const unsigned int nametab_sz = ARRAY_SIZE(nametab);"
