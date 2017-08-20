#ifndef URL_REGEX_HH
#define URL_REGEX_HH

#define USERCHARS       "-[:alnum:]"
#define USERCHARS_CLASS "[" USERCHARS "]"
#define PASSCHARS_CLASS "[-[:alnum:]\\Q,?;.!%$^*&~\"#'\\E]"
#define HOSTCHARS_CLASS "[-[:alnum:]]"
#define HOST            "(?:" HOSTCHARS_CLASS "+(\\." HOSTCHARS_CLASS "+)*)?"
#define PORT            "(?:\\:[[:digit:]]{1,5})?"
#define SCHEME          "(?:[[:alpha:]][+-.[:alnum:]]*:)"
#define USERPASS        USERCHARS_CLASS "+(?:\\:" PASSCHARS_CLASS "+)?"
#define URLPATH         "(?:/[[:alnum:]\\Q-_.!~*'();/?:@&=+$,#%\\E]*)?"

const char * const url_regex = SCHEME "//(?:" USERPASS "\\@)?" HOST PORT URLPATH;

#endif
