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
pointer to a collection of production rules:
```c
typedef struct non_terminal *non_terminal_p;
struct non_terminal
{
	const char *name;     /* Name of the non-terminal */
	rules_p first;        /* Normal rules */
	rules_p recursive;    /* Left-recursive rules */
};
```
## Production rules

For an arbitrary grammar it is possible that an input string can be parsed
in multiple ways. One way to make parsing deterministic is to order the
production rules and select a parsing based on this order. Production rules
that become for other production rules will have precedence. The collection
of production rules will be defined as a list, where `element_p` is the
type for a pointer to elements of a production rule:
```c
typedef struct rules *rules_p;
struct rules
{
	element_p elements;     /* The rule definition */
	rules_p next;           /* Next rule */
};
```

## A production rule

A production rule consists of a list of non-terminals and terminals. The terminals
are characters. We make use of an enumeration `element_kind_t` to represent the
various types of elements in the rule. We also will make use of a union for
effficiently storing the members of the variants defined by the enumeration.
This leads to the following type definitions (which is going to be extended below):
```c
enum element_kind_t
{
	rk_nt,       /* A non-terminal */
	rk_char     /* A character */
};

typedef struct element *element_p;
struct element
{
	enum element_kind_t kind;   /* Kind of element */
	union 
	{
		non_terminal_p non_terminal; /* rk_nt: Pointer to non-terminal */
		char ch;                     /* rk_char: The character */
	} info;
	
	element_p next;             /* Next element in the rule */
};

void element_init(element_p element, enum element_kind_t kind)
{
	element->kind = kind;
	element->next = NULL;
}
```

## Character sets as terminals

In principal, the above is enough to define any grammar, but it will not be very
practical. For example, many programming languages have identifiers and these identifiers
often start with an alphabetic character followed by one or more alphabetic characters or
digits. For this we need non-terminals representing the first and following characters of
the identifiers. For these non-terminals we have to define production rules for each
character that is allowed, which leads to a large number of production rules. To avoid
this, a character set is added as a type of terminal, where ranges of characters can
be added to. For this we add the following functions (which will defined later):
```c
char_set_p new_char_set();
void char_set_add_char(char_set_p char_set, char ch);
void char_set_add_range(char_set_p char_set, char first, char last);
```

Now we can extend the element definiton as follows:
```c
enum element_kind_t
{
	...,
	rk_charset  /* A character set */
};

struct element
{
	...
	union
	{
		...
		char_set_p char_set;         /* rk_charset: Pointer to character set definition */
	} info;
}
```
