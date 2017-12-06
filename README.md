# evev

evev is a tool for monitoring and executing actions based on Linux input events.

## Features
evev was made with flexibility in mind, and as such has several convenient features:

- Event combination expression parser
- Device selection by phy, name or device file
- Scripting friendly event monitoring interface
- Event debounce/long-press support
- Event value comparison


## Config
Default config file location is `/etc/evev/*.cfg` but another path or pattern may be specified with `-c`.  Alternatively, one may use `-e` to specify configuration on the cmdline.

### Config format

The config format is made up of a list of rules/bindings of the form `expression "<=" command`, where the command is terminated by a newline.  Expressions evaluate as booleans.  Expression operations include:
- `a & b`: logical AND
- `a | b`: logical OR
- `a ^ b`: logical XOR
- `!e`: logical NOT
- `e[N]`: debounce/delay (`N` is a positive integer, optionally followed by "s" to indicate seconds, or "ms" to indicate milliseconds (default))
- `e:C`: value comparison (`C` is an integer, optionally prefixed by a comparison operation "eq" (default), "ne", "lt", "gt", "le", or "ge").
- `(e)`: grouping


The full EBNF for reference:
```ebnf
cfg	::= S rule* EOF
rule	::= expr "<=" S cmd
cmd	::= [^\n]* "\n"
expr	::= or
or	::= xor ("|" S xor)*
xor	::= and ("^" S and)*
and	::= pri ("&" S pri)*
pri	::= not | pfix
not	::= "!" S pri
pfix	::= (grp | evt) dur?
dur	::= "[" S NUM (s|ms)? "]" S
grp	::= "(" S expr ")" S
evt	::= SYM cmp?
cmp	::= ":" S ("eq" | "ne" | "lt" | "gt" | "le" | "ge")? "-"? NUM

NUM	::= ("0" [0-7]* | "0x" [0-9A-Fa-f]+ | [1-9] [0-9]*) S
SYM	::= ("KEY" | "BTN" | "ABS" | "SW" | ETC) "_" [A-Z0-9_]+ S
CMNT	::= "#" [^\n]* "\n"
S	::= (CMNT | [ \t\n\r]+)*
EOF	::= !.
```

### Examples
```sh
# Hold CTRL+Enter for 3 seconds to hibernate
((KEY_LEFTCTRL | KEY_RIGHTCTRL) & KEY_ENTER)[3s] <=
	echo disk > /sys/power/state

# Lid switch for 1s suspends
SW_LID[1s] <= echo mem > /sys/power/state

# META+U unmounts /mnt/floppy
(KEY_LEFTMETA | KEY_RIGHTMETA) & KEY_U <= umount /mnt/floppy

# Touch top left of touchpad/touchscreen for 5s to start VPN
(BTN_TOUCH & ABS_X:lt100 & ABS_Y:lt100)[5s] <= systemctl start vpn@home
```
## Usage
```
Usage: evev OPTIONS <event...>

   <event...> can be a pattern in the form of:
       name=<device name>  (e.g name='AT Keyboard')
       phys=<device phys>  (e.g phys='isa0060/input[0-9]')
       dev=<device file>   (e.g dev=/dev/input/event0)
       <device file>       (e.g /dev/input/event0)
   Options:
        -m        monitor mode
        -l        enable logging
        -I        output information about event devices
        -c <cfg>  config location (pattern)
        -e <txt>  inline configuration
        -q        disable non-fatal errors and warnings
        -h        this cruft
        -v        version info
```

## Custom scripting
Prefer to script it yourself?  Go for it!  Here's a simple example:
```bash
#!/bin/bash

device="phys=isa0060/input[0-9]"

declare -A commands

commands["ABS_VOLUME=100"]="$@"

# input: ABS ABS_VOLUME 100
while read etype esym value; do
	cmd="${commands["$esym=$value"]}"
	[ -n "$cmd" ] && $cmd
done << EOC
$(evev -mq "$device")
EOC
```
## Pronunciation & Capitalization
evev may be pronounced and capitalized however you like.  Courtney (the creator) prefers to change pronunciation regularly just to make things more confusing.  Here are a few pronunciations to choose from:
- ee vee ee vee
- eve vee
- ee veev
- eve eve

And some possible capitalizations:
- EveV
- EvEv
- evev
- eveV

