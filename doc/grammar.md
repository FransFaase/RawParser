# Representation of the grammar

Before we can implement the parser, we need to provide a data structure for
defining [the formal grammar](https://en.wikipedia.org/wiki/Formal_grammar).
A formal grammar consists of non-terminals and terminals. Because the parser is a
scannerless parser, it means that the terminals of the grammar will be single characters.
For each non-terminal there will be one or more
[production rules](https://en.wikipedia.org/wiki/Production_(computer_science)),
that specify how the non-terminal can be rewritten in one or more non-terminals
or terminals.

A left-recursive production rule is a production rule where the non-terminal
occurs as the first element of the production rule. The left-recursive production
rules will be stored in a separate list of rules, where the first element (the
recursive non-terminal) is left out of the elements. With this we define a
non-terminal with the following C code, where `rules_p` is the type for a
pointer to a list of rules:
```c
typedef struct non_terminal *non_terminal_p;
struct non_terminal
{
	const char *name;     /* Name of the non-terminal */
	rules_p first;        /* Normal rules */
	rules_p recursive;    /* Left-recursive rules */
};
```

A grammar consists of 
Many programming languages have identifiers and these identifiers often start with
an alphabetic character followed by one or more alphabetic characters or digits. To deal with
ranges of characters we introduce the `char_set_p` type as a pointer to a structure
representing a character set. With this type we define the following functions:
```c
void char_set_add_char(char_set_p char_set, char ch);
void char_set_add_range(char_set_p char_set, char first, char last);
bool char_set_contains(char_set_p char_set, const char ch);
```

