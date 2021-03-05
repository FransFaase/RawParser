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
typedef struct non_terminal non_terminal_t, *non_terminal_p;
struct non_terminal
{
	const char *name;     /* Name of the non-terminal */
	rules_p normal;       /* Normal rules */
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
are characters. There will also be a terminal to indicate the end of the input, such
that we can specify that the end of the input should be included. We make use of an
enumeration `element_kind_t` to represent the various types of elements in the rule.
We also will make use of a union for effficiently storing the members of the variants
defined by the enumeration.
This leads to the following type definitions (which is going to be extended below):
```c
enum element_kind_t
{
	rk_nt,       /* A non-terminal */
	rk_char,     /* A character */
	rk_end       /* End of input */
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
void char_set_add_char(char_set_p char_set, unsigned char ch);
void char_set_add_range(char_set_p char_set, unsigned char first, unsigned char last);
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
};
```

We also add a function to initialize an element:
```c
void element_init(element_p element, enum element_kind_t kind)
{
	element->kind = kind;
	element->next = NULL;
}
```

## Modifiers

[Extended Backus–Naur form](https://en.wikipedia.org/wiki/Extended_Backus%E2%80%93Naur_form) (EBNF)
extents the grammar with modifiers to indicate that an element is optional or can be repeated.
In case an element is optional and/or repeated (a sequence), there is also the question which
of the two options should have prevalence, as it could have been specified if a separate
non-terminal with production rules was used. For this we add three Booleans as modifiers to
the type for an element in a production rule in the following manner:
```c
struct element
{
	...
	bool optional;     /* Whether the element is optional */
	bool sequence;     /* Whether the element is a sequenct */
	bool avoid;        /* Whether the elmennt should be avoided when it is optional and/or sequential */
};

void element_init(element_p element, enum element_kind_t kind)
{
	...
	element->optional = FALSE;
	element->sequence = FALSE;
	element->avoid = FALSE;
}
```
## Chain

A comma separated list is a special kind of sequence, where the elements are 'chained' with
a separate production rule. This is done in the following manner:
```c
struct element
{
	...
	element_p chain_rule;     /* Chain rule, for between the sequential elements */
};

void element_init(element_p element, enum element_kind_t kind)
{
	...
	element->chain_rule = NULL;
}
```

## Grouping

Extended Backus–Naur form also introduces grouping, where an element consists of a number
of nested production rules. For the kinds of elements is extended with grouping element
that has a pointer to a list of production rules. This can be done with the following
extension:
```c
enum element_kind_t
{
	...,
	rk_grouping  /* Grouping of one or more rules */
};

struct element
{
	...
	union
	{
		...
		rules_p rules;               /* rk_grouping: Pointer to the rules */
	} info;
};
```

## A grammar

As said before, a grammar consists of a collection of non-terminals and production
rules. To store a grammar a dictionary of non-terminal names to production rules is
used. The simplest way to implement the dictionary is true a list in the following
manner
```c
typedef struct non_terminal_dict *non_terminal_dict_p;
struct non_terminal_dict
{
	non_terminal_t elem;
	non_terminal_dict_p next;
};
```
And a function to find or add a non-terminal to the dictionary with a given string
representing the name of the non-terminal:
```c
non_terminal_p find_nt(const char *name, non_terminal_dict_p *p_nt);
```

## Defines for defining a grammar

With the above (and some additional functions to allocate and initalize the various
structures) a hard-coded grammar can be specified. With the help of defines, it is
possible to do this in a human readable way.

Take for example the grammar below for white space in the C programming language
with two ways for defining comments, using a kind of ENBF grammar. The C manner
of representing characters is used. A terminal consisting of a single character is
represented with a character between single quotes. A character set is a range of
characters placed between square brackets where the dash is used to specify a
range. The modifiers are specified with `SEQ`, `OPT` and `AVOID`. This results in
the following production rule:
```
white_space ::=
    ( [\t\n\r ]
    | '/' '/' [\t\r -\177] SEQ OPT '\n'
    | '/' '*' [\t\n\r -\177] SEQ OPT AVOID '*' '/'
    ) SEQ OPT
```
With the right defines, it is possible to hard-code this grammar in the following
manner in the function below:
```c
void white_space_grammar(non_terminal_dict_p *all_nt)
{
    HEADER(all_nt)
    
    NT_DEF("white_space")
        RULE
            { GROUPING
                RULE /* for the usual white space characters */
                    CHARSET ADD_CHAR('\t') ADD_CHAR('\n') ADD_CHAR('\r') ADD_CHAR(' ')
                RULE /* for the single line comment starting with two slashes */
                    CHAR('/')
                    CHAR('/')
                    CHARSET ADD_CHAR('\t') ADD_CHAR('\r') ADD_RANGE(' ', 255) SEQ OPT
                    CHAR('\n')
                RULE /* for the traditional C-comment (using avoid modifier) */
                    CHAR('/')
                    CHAR('*')
                    CHARSET ADD_CHAR('\t') ADD_CHAR('\n') ADD_CHAR('\r') ADD_RANGE(' ', 255) SEQ OPT AVOID
                    CHAR('*')
                    CHAR('/')
            } SEQ OPT
}
```

The defines that needed to define a grammar in this manner, are:
```c
#define HEADER(N) non_terminal_dict_p *_nt = N; non_terminal_p nt; rules_p* ref_rule; rules_p* ref_rec_rule; rules_p rules; element_p* ref_element; element_p element;
#define NT_DEF(N) nt = find_nt(N, _nt); ref_rule = &nt->normal; ref_rec_rule = &nt->recursive;
#define RULE rules = *ref_rule = new_rule(); ref_rule = &rules->next; ref_element = &rules->elements;
#define REC_RULE rules = *ref_rec_rule = new_rule(); ref_rec_rule = &rules->next; ref_element = &rules->elements;
#define _NEW_GR(K) element = *ref_element = new_element(K); ref_element = &element->next;
#define NT(N) _NEW_GR(rk_nt) element->info.non_terminal = find_nt(N, _nt);
#define END _NEW_GR(rk_end)
#define SEQ element->sequence = TRUE;
#define CHAIN element_p* ref_element = &element->chain_rule; element_p element;
#define OPT element->optional = TRUE;
#define AVOID element->avoid = TRUE;
#define CHAR(C) _NEW_GR(rk_char) element->info.ch = C;
#define CHARSET _NEW_GR(rk_charset) element->info.char_set = new_char_set();
#define ADD_CHAR(C) char_set_add_char(element->info.char_set, C);
#define ADD_RANGE(F,T) char_set_add_range(element->info.char_set, F, T);
#define GROUPING _NEW_GR(rk_grouping) element->info.rules = new_rule(); rules_p* ref_rule = &element->info.rules; rules_p rules; element_p* ref_element; element_p element;
```
These defines, make use of the following functions:
```c
rules_p new_rule();
element_p new_element(enum element_kind_t kind);
```

# Implementations

Below we give the implementations details for the missing definition
## Some basic definition

```c
//#define NULL 0

typedef int bool;
#define TRUE 1
#define FALSE 0

typedef unsigned char byte;

#define MALLOC(T) (T*)malloc(sizeof(T))
#define MALLOC_N(N,T)  (T*)malloc((N)*sizeof(T))
#define STR_MALLOC(N) (char*)malloc((N)+1)
#define STRCPY(D,S) D = (char*)malloc(strlen(S)+1); strcpy(D,S)
#define FREE(X) free(X)

```

## Character set implementation

```c
typedef struct char_set *char_set_p;
struct char_set
{
	byte bitvec[32];
};

char_set_p new_char_set()
{
	char_set_p char_set = MALLOC(struct char_set);
	for (int i = 0; i < 32; i++)
		char_set->bitvec[i] = 0;
	return char_set;
}

bool char_set_contains(char_set_p char_set, const char ch) { return (char_set->bitvec[((byte)ch) >> 3] & (1 << (((byte)ch) & 0x7))) != 0; }
void char_set_add_char(char_set_p char_set, char ch) { char_set->bitvec[((byte)ch) >> 3] |= 1 << (((byte)ch) & 0x7); }
void char_set_add_range(char_set_p char_set, char first, char last)
{
	byte ch = (byte)first;
	for (; ((byte)first) <= ch && ch <= ((byte)last); ch++)
		char_set_add_char(char_set, ch);
}

```

## Grammar creation implementation

```c

non_terminal_p find_nt(const char *name, non_terminal_dict_p *p_nt)
{
   while (*p_nt != NULL && (*p_nt)->elem.name != name && strcmp((*p_nt)->elem.name, name) != 0)
		p_nt = &((*p_nt)->next);

   if (*p_nt == NULL)
   {   *p_nt = MALLOC(struct non_terminal_dict);
	   (*p_nt)->elem.name = name;
	   (*p_nt)->elem.normal = NULL;
	   (*p_nt)->elem.recursive = NULL;
	   (*p_nt)->next = NULL;
   }
   return &(*p_nt)->elem;
}

rules_p new_rule()
{
	rules_p rule = MALLOC(struct rules);
	rule->elements = NULL;
	rule->next = NULL;
	return rule;
}

element_p new_element(enum element_kind_t kind)
{
	element_p element = MALLOC(struct element);
	element_init(element, kind);
	return element;
}
```
