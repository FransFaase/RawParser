/* RawParser -- A 'raw' parser         Copyright (C) 2021 Frans Faase

   This shows how to implement a grammar driven, scannerless parser in C
   in a number of incremental steps going from simple to complex.

   Grammar driven means that this is not a compiler that reads a grammar
   and either generates code (implementing the parser) or a set of tables
   to drive a parsing algorithm. (An example of the latter is yacc/bison.)
   Instead the parsing algorithm directly operates on the grammar
   specification, which allows to implement a rich grammar with optional,
   sequential and grouping, of grammar elements. This specification is
   represented by structs that point to each other. The construction of
   the grammar is aided by some clever defines to increase readability.

   It being scannerless means that the parser operates on a single unified
   grammar description for both the scanning and parsing aspects. In this
   grammar description, function pointers are used to define actions to
   be taken during parsing to construct the resuling abstract syntax tree.
   
   The code has been structured to make it more accessible. For this
   purpose narrative comments have been added and examples of usage are
   given.
   
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

GNU General Public License:
   http://www.iwriteiam.nl/GNU.txt

*/

#define VERSION "0.1 of January 2021."

/* 
	First some standard includes and definitions.
*/

#include <stdio.h>
#include <malloc.h>
#include <string.h>

#ifndef NULL
#define NULL 0
#endif

typedef int bool;
#define TRUE 1
#define FALSE 0

typedef unsigned char byte;

#if TRACE_ALLOCATIONS

void *my_malloc(size_t size, unsigned int line)
{
	void *p = malloc(size);
	fprintf(stdout, "At line %lu: allocated %lu bytes %p\n", line, size, p);
	return p;
}

void my_free(void *p, unsigned int line)
{
	fprintf(stdout, "At line %lu: freed %p\n", line, p);
	free(p);
}

#else

#define my_malloc(X,L) malloc(X)
#define my_free(X,L) free(X)

#endif

#define MALLOC(T) (T*)my_malloc(sizeof(T), __LINE__)
#define MALLOC_N(N,T)  (T*)my_malloc((N)*sizeof(T), __LINE__)
#define STR_MALLOC(N) (char*)my_malloc((N)+1, __LINE__)
#define STRCPY(D,S) D = (char*)my_malloc(strlen(S)+1, __LINE__); strcpy(D,S)
#define FREE(X) my_free(X, __LINE__)


/*
	Internal representation parsing rules
	~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	The following section of the code deals with the internal representation of
	the parsing rules as they are used by the parsing routines.
	
	The grammar is an extended BNF grammar, which supports optional elements,
	sequences of elements (with an optional chain rule) and grouping within the
	grammar rules. Because the scanner is intergrated with the parser, the
	terminals are defined with characters and character sets. The grammar does
	support direct left recursion. (The parsing algorithm cannot deal with
	indirect left recursion.) For a non-terminal these left recursive grammar
	rules are stored separately without mentioning the recursive non-terminal
	in the rule.
	
	The grammar consists thus of a list of non-terminals, where each
	non-terminal has two list of rules (one for non-left recursive rules
	and one for left recursive rules). Each rule defines a rule, which
	consists of list of grammar elements. An element can be one of:
	- character,
	- character set,
	- end of text,
	- non-terminal, or
	- grouping of rules.
	An element can have modifiers for making the element optional or a sequence.
	It is also possible to specify that an optional and/or sequential element
	should be avoided in favour of the remaining rule.
	With a sequential element it is possible to define a chain rule, which is
	to appear between the elements. An example of this is a comma separated
	list of elements, where the comma (and possible white space) is the chain
	rule.
	Each element has a number of function pointers, which can be used to specify
	functions that should be called to process the parsing results. Furthermore,
	each rule has a function pointer, to specify the function that should
	be called at the end of the rule to process the result.
	
	An example for a white space grammar will follow.
*/

/*  Forward declarations of types of the grammar definition.  */

typedef struct non_terminal non_terminal_t, *non_terminal_p;
typedef struct rules *rules_p;
typedef struct element *element_p;
typedef struct char_set *char_set_p;
typedef struct result result_t, *result_p;
typedef struct text_pos text_pos_t, *text_pos_p;

/*  Definition for a non-terminal  */

struct non_terminal
{
	const char *name;     /* Name of the non-terminal */
	rules_p normal;       /* Normal rules */
	rules_p recursive;    /* Left-recursive rules */
};

typedef struct non_terminal_dict *non_terminal_dict_p;
struct non_terminal_dict
{
	non_terminal_t elem;
	non_terminal_dict_p next;
};

/*  - Function to find a non-terminal on a name or add a new to end of list */

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

/*  Definition of an rule  */

typedef bool (*end_function_p)(const result_p rule_result, void* data, result_p result);

struct rules
{
	element_p elements;            /* The rule definition */

	/* Function pointer to an optional function that is to be called when rule
	   is parsed. Input arguments are the result of the rule and a pointer to
	   some additional data. The output is the result to be returned by the
	   rule. When the function pointer is null, the result of the rule is
	   taken as the result of the rule. */
	end_function_p end_function;
	void *end_function_data;      /* Pointer to additional data which is passed to end_function */

	/* (Only for left-recursive rules.) Function pointer to an optional
	   Boolean function that is called at the start of the rule to add the
	   already parsed left-recursive rule to the result to be passed to the
	   remained of the rule (of this rule). of this rule. When the
	   function returns false, parsing fails. When the function pointer is null,
	   it is equivalent with a function that always returns true and does not
	   set the result, thus discarding the already parsed left-recursive rule.
	*/
	bool (*rec_start_function)(result_p rec_result, result_p result);

	rules_p next;           /* Next rule */
};

/*  - Function to create a new rule */

rules_p new_rule()
{
	rules_p rule = MALLOC(struct rules);
	rule->elements = NULL;
	rule->end_function = NULL;
	rule->end_function_data = NULL;
	rule->rec_start_function = NULL;
	rule->next = NULL;
	return rule;
}

/*  
	Defintion of an element of a rule.
*/

enum element_kind_t
{
	rk_nt,       /* A non-terminal */
	rk_grouping, /* Grouping of one or more rules */
	rk_char,     /* A character */
	rk_charset,  /* A character set */
	rk_end,      /* End of input */
	rk_term      /* User defined terminal scan function */
};

struct element
{
	enum element_kind_t kind;   /* Kind of element */
	bool optional;              /* Whether the element is optional */
	bool sequence;              /* Whether the element is a sequenct */
	bool avoid;                 /* Whether the elmennt should be avoided when it is optional and/or sequential */
	element_p chain_rule;       /* Chain rule, for between the sequential elements */
	union 
	{   non_terminal_p non_terminal; /* rk_nt: Pointer to non-terminal */
		rules_p rules;               /* rk_grouping: Pointer to the rules */
		char ch;                     /* rk_char: The character */
		char_set_p char_set;         /* rk_charset: Pointer to character set definition */
		const char *(*terminal_function)(const char *input, result_p result);
		                             /* rk_term: Pointer to user defined terminal scan function */
	} info;

	/* Function pointer to an optional Boolean function that is called after the
	   character is parsed, to combine the result of the previous elements with
	   the character into the result passed to the remainder of the rule. (When
	   the element is a sequence, the previous result is the result of the
	   previous characters in the sequence. For more details see the description
	   of the function pointers begin_seq_function and add_seq_function.) When
	   the function returns false, parsing fails. When the function pointer is
	   null, it is equivalent with a function that always returns true and simple
	   sets the result as the result of the previous element, thus discarding the
	   result of the element. This is, for example used, when the element is a
	   literal character. */
	bool (*add_char_function)(result_p prev, char ch, result_p result);

	/* Function pointer to an optional Boolean function that is called after the
	   element is parsed. When the function returns false, parsing fails. The
	   function is called with the result of the element and a pointer to an
	   additional argument. When the function pointer is null, it is equivalent
	   with a function that always returns true.
	   A typical usage of this function is to check if a parsed identified is
	   a certain keyword or not a keyword at all. */
	bool (*condition)(result_p result, const void *argument);
	const void *condition_argument;

	/* Function pointer to an optional Boolean function that is called after the
	   element is parsed and after the optional condition function has been
	   called, to combine the result of the previous elements with the element
	   into the result to be passed to the remainder of the rule. (When the
	   element is a sequence, the previous result is the result of the previous
	   elements in the sequence. For more details see the description of the
	   function pointers begin_seq_function and add_seq_function.) When the
	   function returns false, parsing fails. When the function pointer is null,
	   it is equivalent with a function that always returns true and simple sets
	   the result as the result of the previous element, thus discarding the
	   result of the element. */
	bool (*add_function)(result_p prev, result_p elem, result_p result);

	/* Function pointer to an optional Boolean function that is called when an
	   optional element is skipped, to apply this to the result of the
	   previous elements into the result to be passed to the remainder of the
	   rule. When the function returns false, parsing fails. When the function
	   pointer is null, the add_function with an empty result acts as a
	   fallback. */
	bool (*add_skip_function)(result_p prev, result_p result);

	/* Function pointer to an optional void function that is called at the
	   start of parsing an element that is a sequence which is given the result
	   of the previous elements and which result is passed to the first a
	   call of to function processing the elements of the sequence, for
	   example add_char_function or add_function. When the function pointer
	   is null, an initial result is passed to the first element of the
	   sequence. */
	void (*begin_seq_function)(result_p prev, result_p seq);

    /* Function pointer to an optional Boolean function that is called after
       the complete sequence of elements has been parsed, to combine it with
       the result of the previous elements into the result to be passed to
       the remainder of the rule. When the function returns false, parsing
       fails. When the function pointer is null, it is equivalent with a
       function that always returns true and simple sets the result as the
       result of the previous element, thus discarding the result of the
       element.*/
	bool (*add_seq_function)(result_p prev, result_p seq, result_p result);

	/* Function pointer to an optional void function that is called with
	   the position (line, column numbers) at the start of parsing the
	   element with the result that is passed to the remainder of the
	   rule, thus after the previous functions have been called. */
	void (*set_pos)(result_p result, text_pos_p ps);

	const char *expect_msg;     /* For error reporting */
	
	element_p next;             /* Next element in the rule */
};

/*
	- Function to create new element
*/

void element_init(element_p element, enum element_kind_t kind)
{
	element->kind = kind;
	element->next = NULL;
	element->optional = FALSE;
	element->sequence = FALSE;
	element->avoid = FALSE;
	element->chain_rule = NULL;
	element->add_char_function = 0;
	element->condition = 0;
	element->condition_argument = NULL;
	element->add_function = 0;
	element->add_skip_function = 0;
	element->begin_seq_function = 0;
	element->add_seq_function = 0;
	element->set_pos = 0;
}
	
element_p new_element(enum element_kind_t kind)
{
	element_p element = MALLOC(struct element);
	element_init(element, kind);
	return element;
}

/*  Definition of a character set (as a bit vector)  */

struct char_set
{
	byte bitvec[32];
};

/*
	- Function to create new character set
*/

char_set_p new_char_set()
{
	char_set_p char_set = MALLOC(struct char_set);
	for (int i = 0; i < 32; i++)
		char_set->bitvec[i] = 0;
	return char_set;
}

/*
	- Functions belonging to character sets
*/

bool char_set_contains(char_set_p char_set, const char ch) { return (char_set->bitvec[((byte)ch) >> 3] & (1 << (((byte)ch) & 0x7))) != 0; }
void char_set_add_char(char_set_p char_set, char ch) { char_set->bitvec[((byte)ch) >> 3] |= 1 << (((byte)ch) & 0x7); }
void char_set_remove_char(char_set_p char_set, char ch) { char_set->bitvec[((byte)ch) >> 3] &= ~(1 << (((byte)ch) & 0x7)); }
void char_set_add_range(char_set_p char_set, char first, char last)
{
	byte ch = (byte)first;
	for (; ((byte)first) <= ch && ch <= ((byte)last); ch++)
		char_set_add_char(char_set, ch);
}


/*
	- Functions for printing representation parsing rules
*/

void element_print(FILE *f, element_p element);

void rules_print(FILE *f, rules_p rule)
{
	bool first = TRUE;

	for (; rule; rule = rule->next)
	{   
		if (!first)
			fprintf(f, "|");
		first = FALSE;
		element_print(f, rule->elements);
	}
}

void print_c_string_char(FILE *f, char ch)
{
	switch (ch)
	{
		case '\0': fprintf(f, "\\0"); break;
		case '\a': fprintf(f, "\\a"); break;
		case '\b': fprintf(f, "\\b"); break;
		case '\n': fprintf(f, "\\n"); break;
		case '\r': fprintf(f, "\\r"); break;
		case '\t': fprintf(f, "\\t"); break;
		case '\v': fprintf(f, "\\v"); break;
		case '\\': fprintf(f, "\\\\"); break;
		case '-':  fprintf(f, "\\-"); break;
		case ']':  fprintf(f, "\\]"); break;
		default:
			if (ch < ' ')
				fprintf(f, "\\%03o", ch);
			else
				fprintf(f, "%c", ch);
	}
}

void element_print(FILE *f, element_p element)
{   
	if (element == NULL)
		return;

	switch(element->kind)
	{
		case rk_nt:
			fprintf(f, "%s ", element->info.non_terminal->name);
			break;
		case rk_grouping:
			fprintf(f, "(");
			rules_print(f, element->info.rules);
			fprintf(f, ")");
			break;
		case rk_char:
			fprintf(f, "'%c' ", element->info.ch);
			break;
		case rk_charset:
			fprintf(f, "[");
			unsigned char from = 255;
			for (unsigned char ch = 0; ; ch++)
			{
				if (char_set_contains(element->info.char_set, ch))
				{
					if (from == 255)
					{
						from = ch;
						print_c_string_char(f, ch);
					}
				}
				else if (from < 255)
				{
					if (ch > from+1)
					{
						if (ch > from+2)
							fprintf(f, "-");
						print_c_string_char(f, ch-1);
					}
					from = 255;
				}
				if (ch == 255)
					break;
			}
			if (from < 255)
				fprintf(f, "-\\377");
			fprintf(f, "] ");
			break;
		case rk_end:
			fprintf(f, "<eof> ");
			break;
		case rk_term:
			fprintf(f, "<term> ");
			break;
	}

	if (element->sequence)
	{   if (element->chain_rule == NULL)
			fprintf(f, "SEQ ");
		else
		{
			fprintf(f, "CHAIN (");
			element_print(f, element->chain_rule);
			fprintf(f, ")");
		}
	}
	if (element->optional)
		fprintf(f, "OPT ");
	if (element->avoid)
		fprintf(f, "AVOID ");
	element_print(f, element->next);
}

/*  Some macro definitions for defining a grammar more easily.  */

#define HEADER(N) non_terminal_dict_p *_nt = N; non_terminal_p nt; rules_p* ref_rule; rules_p* ref_rec_rule; rules_p rules; element_p* ref_element; element_p element;
#define NT_DEF(N) nt = find_nt(N, _nt); ref_rule = &nt->normal; ref_rec_rule = &nt->recursive;
#define RULE rules = *ref_rule = new_rule(); ref_rule = &rules->next; ref_element = &rules->elements;
#define REC_RULE(E) rules = *ref_rec_rule = new_rule(); rules->rec_start_function = E; ref_rec_rule = &rules->next; ref_element = &rules->elements;
#define _NEW_GR(K) element = *ref_element = new_element(K); ref_element = &element->next;
#define NTF(N,F) _NEW_GR(rk_nt) element->info.non_terminal = find_nt(N, _nt); element->add_function = F;
#define END _NEW_GR(rk_end)
#define SEQ(S,E) element->sequence = TRUE; element->begin_seq_function = S; element->add_seq_function = E;
#define CHAIN element_p* ref_element = &element->chain_rule; element_p element;
#define OPT(F) element->optional = TRUE; element->add_skip_function = F;
#define AVOID element->avoid = TRUE;
#define SET_PS(F) element->set_pos = F;
#define CHAR(C) _NEW_GR(rk_char) element->info.ch = C;
#define CHARSET(F) _NEW_GR(rk_charset) element->info.char_set = new_char_set(); element->add_char_function = F;
#define ADD_CHAR(C) char_set_add_char(element->info.char_set, C);
#define REMOVE_CHAR(C) char_set_remove_char(element->info.char_set, C);
#define ADD_RANGE(F,T) char_set_add_range(element->info.char_set, F, T);
#define END_FUNCTION(F) rules->end_function = F;
#define GROUPING _NEW_GR(rk_grouping) element->info.rules = new_rule(); rules_p* ref_rule = &element->info.rules; rules_p rules; element_p* ref_element; element_p element;
		


/*
	Example of defining white space grammar with comments
	~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	In this example, white space does not have a result, thus all function
	pointers can be left 0. White space is defined as a (possible empty)
	sequence of white space characters, the single line comment and the
	traditional C-comment. '{ GROUPING' and '}' are used to define a
	grouping. The grouping contains three rules.
*/

void white_space_grammar(non_terminal_dict_p *all_nt)
{
	HEADER(all_nt)
	
	NT_DEF("white_space")
		RULE
			{ GROUPING
				RULE /* for the usual white space characters */
					CHARSET(0) ADD_CHAR(' ') ADD_CHAR('\t') ADD_CHAR('\n')
				RULE /* for the single line comment starting with two slashes */
					CHAR('/')
					CHAR('/')
					CHARSET(0) ADD_RANGE(' ', 255) ADD_CHAR('\t') SEQ(0, 0) OPT(0)
					CHAR('\n')
				RULE /* for the traditional C-comment (using avoid modifier) */
					CHAR('/')
					CHAR('*')
					CHARSET(0) ADD_RANGE(' ', 255) ADD_CHAR('\t') ADD_CHAR('\n') SEQ(0, 0) OPT(0) AVOID
					CHAR('*')
					CHAR('/')
			} SEQ(0, 0) OPT(0)
}


/*
	Example of defining a positive whole number grammar
	~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	In this example, a grammar is given for a positive whole number, and
	explained how a result can be returned. A whole number is represented
	by a sequence of characters in the range '0' to '9'. There are two
	functions needed: One that takes a character and calculates the
	resulting number when that character is added at the back of a number.
	One that transfers the result of the sequence to result of the rule.
	The first function needs to be passed as an argument to the CHARSET
	define. The second function needs to be passed as the second argument
	to the SEQ define.
	If no function is set for processing the result of a rule, then the
	result of the last element is returned.
*/

bool number_add_char(result_p prev, char ch, result_p result);
bool use_sequence_result(result_p prev, result_p seq, result_p result);

void number_grammar(non_terminal_dict_p *all_nt)
{
	HEADER(all_nt)
	
	NT_DEF("number")
		RULE
			CHARSET(number_add_char) ADD_RANGE('0', '9') SEQ(0, use_sequence_result)
}

/*
	To implement the two functions, some definitions are needed, which
	will be explained below.
	
	
	Output stream
	~~~~~~~~~~~~~
	
	We first define an interface for an output stream, which later
	can be implemented as either outputting to a file or a string buffer.
*/

typedef struct ostream ostream_t, *ostream_p;
struct ostream
{
	void (*put)(ostream_p ostream, char ch);
};

void ostream_put(ostream_p ostream, char ch)
{
	ostream->put(ostream, ch);
}

void ostream_puts(ostream_p ostream, const char *s)
{
	while (*s != '\0')
		ostream_put(ostream, *s++);
}

/*
	Result
	~~~~~~

	Because the parser algorithm is agnostic to the types of results that
	are used by grammar rule, a void pointer is used. Reference counting
	is often used to manage dynamically allocated memory. It is a good
	idea to group the void pointer with functions to increment and decrement
	the reference count. The struct 'result' below also adds a function
	pointer to a print function.
*/

struct result
{	
	void *data;
	void (*inc)(void *data);
	void (*dec)(void *data);
	void (*print)(void *data, ostream_p ostream);
};

/*
	- Function to initialize a result
*/

void result_init(result_p result)
{
	result->data = NULL;
	result->inc = 0;
	result->dec = 0;
	result->print = 0;
}

/*
	- Function to assign result to another result
*/

void result_assign(result_p trg, result_p src)
{
	result_t old_trg = *trg;
	if (src->inc != 0)
		src->inc(src->data);
	*trg = *src;
	if (old_trg.dec != 0)
		old_trg.dec(old_trg.data);
}

/*
	- Function to transfer the result to another result.
	  (The source will be initialized.)
*/

void result_transfer(result_p trg, result_p src)
{
	result_t old_trg = *trg;
	*trg = *src;
	result_init(src);
	if (old_trg.dec != 0)
		old_trg.dec(old_trg.data);
}

/*
	- Function to release the result
*/

void result_release(result_p result)
{
	if (result->dec != 0)
		result->dec(result->data);
	result->data = NULL;
}

/*
	- Function to print the result
*/

void result_print(result_p result, ostream_p ostream)
{
	if (result->print == NULL)
		ostream_puts(ostream, "<>");
	else
		result->print(result->data, ostream);
}


/*
	- Two macro definitions which should be used a the start and end of
	  the scope of a result variable
*/

#define DECL_RESULT(V) result_t V; result_init(&V);
#define DISP_RESULT(V) result_release(&V);


/*
	- Function for using result of a sequence
	  (to be used as value for the add_seq_function function pointer)
*/

bool use_sequence_result(result_p prev, result_p seq, result_p result)
{
	result_assign(result, seq);
	return TRUE;
}

/*
	Base for reference counting results
	~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	Because there is usually more than one type that needs to implement
	reference counting, it is a good idea to define a base struct for it.
	When this is used as the type of the first member in a struct
	for a result, then the reference counting functions can be called
	on these.
*/

bool debug_allocations = FALSE;

typedef struct
{
	unsigned long cnt;     /* A reference count */

	/* Function pointer to an optional void function that is called
	   right before the data is freed. This is only needed when the
	   data contains pointers to other pieces of data for which
	   reference counts need to be decremented.
	*/
	void (*release)(void *);
} ref_counted_base_t, *ref_counted_base_p;

void ref_counted_base_inc(void *data) { ((ref_counted_base_p)data)->cnt++; }
void ref_counted_base_dec(void *data)
{
	if (--((ref_counted_base_p)data)->cnt == 0)
	{
		if (debug_allocations) fprintf(stdout, "Free %p\n", data);
		if (((ref_counted_base_p)data)->release != 0)
			((ref_counted_base_p)data)->release(data);
		else
			FREE(data);
	}
}

void result_assign_ref_counted(result_p result, void *data, void (*print)(void *data, ostream_p ostream))
{
	if (debug_allocations) fprintf(stdout, "Allocated %p\n", data);
	((ref_counted_base_p)data)->cnt = 1;
	result->data = data;
	result->inc = ref_counted_base_inc;
	result->dec = ref_counted_base_dec;
	result->print = print;
}

/*
	Number result
	~~~~~~~~~~~~~
	
	The struct for representing the number has but a single member
	(besides the member for the reference counting base).
*/

typedef struct number_data
{
	ref_counted_base_t _base;
	long num;
} *number_data_p;

#define NUMBER_DATA_NUM(R) (((number_data_p)(R)->data)->num)

void number_print(void *data, ostream_p ostream)
{
	char buffer[41];
	snprintf(buffer, 40, "number %ld", ((number_data_p)data)->num);
	ostream_puts(ostream, buffer);
}

void new_number_data(result_p result)
{
	number_data_p number_data = MALLOC(struct number_data);
	number_data->_base.release = 0;
	result_assign_ref_counted(result, number_data, number_print);
}


/*
	There are actually two ways to implement the function for adding
	a character to a number. The first time the function is called
	the data pointer of the previous result, will be 0. The first
	solution, allocates new memory each time the function is called.
	The reference counting will take care that after the whole
	rule has been parsed all intermediate results will be freed again.
	The second solution, only allocates memory once. In this case this
	is possible, because the parser will not back-track during parsing
	the number. Both solutions are given in the function below.
*/

bool number_add_char(result_p prev, char ch, result_p result)
{
#if 0 /* Allocating a result for each intermediate result */
	new_number_data(result);
	long num = prev->data != NULL ? NUMBER_DATA_NUM(prev) : 0;
	NUMBER_DATA_NUM(result) = 10 * num + ch - '0';
#else /* Allocating the result but once */
	if (prev->data == NULL)
	{
		new_number_data(result);
		NUMBER_DATA_NUM(result) = ch - '0';
	}
	else
	{
		result_assign(result, prev);
		NUMBER_DATA_NUM(result) = 10 * NUMBER_DATA_NUM(result) + ch - '0';
	}
#endif
	return TRUE;
}

/*
	Implementing a back-tracking parser on a text buffer
	~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	The most 'simple' parsing algorithm, is a back-tracking recursive decent
	parser on a text buffer that is stored in memory. Nowadays memory is
	cheap and storing whole files as strings in memory is usually no problem
	at all. (The hardest part about this parser is the reference counting of
	the results.)
	
	This parser will simply take the grammar specification and try to parse
	the text in the buffer. If it fails at some point, it will simply
	back-track to where it started the current rule and try the next
	rule. It continues doing so until it parses the whole contents or
	fails after having tried all (nested) rules.
	
	Below, we first define a text position and a text buffer that will be
	used by the back-tracking parser.
	
*/

struct text_pos
{
	size_t pos        ;       /* Positive offset from the start of the file */
	unsigned int cur_line;    /* Line number (1-based) with the position */
	unsigned int cur_column;  /* Column number (1-based) with the position */
};

typedef struct
{
	const char *buffer;     /* String containting the input text */
	size_t buffer_len;      /* Length of the input text */
	text_pos_t pos;         /* Current position in the input text */
	const char *info;       /* Contents starting at the current position */
	unsigned int tab_size;  /* Tabs are on multiples of the tab_size */
} text_buffer_t, *text_buffer_p;

void text_buffer_assign_string(text_buffer_p text_buffer, const char* text)
{
	text_buffer->tab_size = 4;
	text_buffer->buffer_len = strlen(text);
	text_buffer->buffer = text;
	text_buffer->info = text_buffer->buffer;
	text_buffer->pos.pos = 0;
	text_buffer->pos.cur_line = 1;
	text_buffer->pos.cur_column = 1;
}

void text_buffer_next(text_buffer_p text_buffer)
{
	if (text_buffer->pos.pos < text_buffer->buffer_len)
	{
	  switch(*text_buffer->info)
	  {   case '\t':
			  text_buffer->pos.cur_column += text_buffer->tab_size - (text_buffer->pos.cur_column - 1) % text_buffer->tab_size;
			  break;
		  case '\n':
			  text_buffer->pos.cur_line++;
			  text_buffer->pos.cur_column = 1;
			  break;
		  default:
			  text_buffer->pos.cur_column++;
			  break;
	  }
	  text_buffer->pos.pos++;
	  text_buffer->info++;
	}
}

bool text_buffer_end(text_buffer_p text_buffer) {
 	return text_buffer->pos.pos >= text_buffer->buffer_len;
}

void text_buffer_set_pos(text_buffer_p text_file, text_pos_p text_pos)
{
	if (text_file->pos.pos == text_pos->pos)
		return;
	text_file->pos = *text_pos;
	text_file->info = text_file->buffer + text_pos->pos;
}

/*
	Caching intermediate parse states
	~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	One way to improve the performance of back-tracking recursive-descent
	parser is to use a cache were intermediate results are stored.
	Because there are various caching strategies an abstract interface
	for caching is provided. The parser struct, to be defined below, has
	a pointer to a function that, if not NULL, is called to query the
	cache and return a cache item. As long as the success status is
	unknown, the cache item may not be freed from memory (during a
	successive call to the function).
*/

enum success_t { s_unknown, s_fail, s_success } ;

typedef struct
{
	enum success_t success;  /* Could said non-terminal be parsed from position */
	result_t result;         /* If so, what result did it produce */
	text_pos_t next_pos;     /* and from which position (with line and column numbers) should parsing continue */
} cache_item_t, *cache_item_p;

/*
	For debugging the parser
	~~~~~~~~~~~~~~~~~~~~~~~~
*/

int depth = 0;
bool debug_parse = FALSE;
bool debug_nt = FALSE;
ostream_p stdout_stream;

#define DEBUG_ENTER(X) if (debug_parse) { DEBUG_TAB; printf("Enter: %s", X); depth += 2; }
#define DEBUG_ENTER_P1(X,A) if (debug_parse) { DEBUG_TAB; printf("Enter: "); printf(X,A); depth += 2; }
#define DEBUG_EXIT(X) if (debug_parse) { depth -=2; DEBUG_TAB; printf("Leave: %s", X); }
#define DEBUG_EXIT_P1(X,A) if (debug_parse) { depth -=2; DEBUG_TAB; printf("Leave: "); printf(X,A); }
#define DEBUG_TAB if (debug_parse) printf("%*.*s", depth, depth, "")
#define DEBUG_NL if (debug_parse) printf("\n")
#define DEBUG_PT(X) if (debug_parse) result_print(X, stdout_stream);
#define DEBUG_PO(X) if (debug_parse) rules_print(stdout, X)
#define DEBUG_PR(X) if (debug_parse) element_print(stdout, X)
#define DEBUG_(X)  if (debug_parse) printf(X)
#define DEBUG_P1(X,A) if (debug_parse) printf(X,A)


/*
	Parser struct definition
	~~~~~~~~~~~~~~~~~~~~~~~~
*/


typedef struct
{
	text_buffer_p text_buffer;
	const char *current_nt;
	cache_item_p (*cache_hit_function)(void *cache, size_t pos, const char *nt);
	void *cache;
} parser_t, *parser_p;

void parser_init(parser_p parser, text_buffer_p text_buffer)
{
	parser->text_buffer = text_buffer;
	parser->current_nt = NULL;
	parser->cache_hit_function = 0;
	parser->cache = NULL;
}

/*
	Parsing functions
	~~~~~~~~~~~~~~~~~
	
	The parsing functions are described top-down, starting with the function
	to parse a non-terminal, which is the top-level function to be called to
	parse a text buffer.
	
*/

bool parse_rule(parser_p parser, element_p element, const result_p prev_result, rules_p rules, result_p rule_result);

bool parse_nt(parser_p parser, non_terminal_p non_term, result_p result)
{
	const char *nt = non_term->name;

	DEBUG_ENTER_P1("parse_nt(%s)", nt); DEBUG_NL;

	/* First try the cache (if available) */
	cache_item_p cache_item = NULL;
	if (parser->cache_hit_function != NULL)
	{
		cache_item = parser->cache_hit_function(parser->cache, parser->text_buffer->pos.pos, nt);
		if (cache_item != NULL)
		{
			if (cache_item->success == s_success)
			{
				DEBUG_EXIT_P1("parse_nt(%s) SUCCESS = ", nt);  DEBUG_PT(&cache_item->result)  DEBUG_NL;
				result_assign(result, &cache_item->result);
				text_buffer_set_pos(parser->text_buffer, &cache_item->next_pos);
				return TRUE;
			}
			else if (cache_item->success == s_fail)
			{
				DEBUG_EXIT_P1("parse_nt(%s) FAIL", nt);  DEBUG_NL;
				return FALSE;
			}
			cache_item->success = s_fail; // To deal with indirect left-recurssion
		}
	}
	
	/* Set current non-terminal on parser */
	const char *surr_nt = parser->current_nt;
	parser->current_nt = nt;

	if (debug_nt)
	{   printf("%*.*s", depth, depth, "");
		printf("Enter: %s\n", nt);
		depth += 2; 
	}

	/* Try the normal rules in order of declaration */
	bool parsed_a_rule = FALSE;
	for (rules_p rule = non_term->normal; rule != NULL; rule = rule->next )
	{
		DECL_RESULT(start)
		if (parse_rule(parser, rule->elements, &start, rule, result))
		{
			parsed_a_rule = TRUE;
			break;
		}
	}
	
	if (!parsed_a_rule)
	{
		/* No rule was succesful */
		DEBUG_EXIT_P1("parse_nt(%s) - failed", nt);  DEBUG_NL;
		if (debug_nt)
		{   depth -= 2;
			printf("%*.*s", depth, depth, "");
			printf("Failed: %s\n", nt);
		}
		
		/* Restore current non-terminal to its previous value */
		parser->current_nt = surr_nt;
		
		return FALSE;
	}
	
	/* Now that a normal rule was succesfull, repeatingly try left-recursive rules */
	while (parsed_a_rule)
	{
		parsed_a_rule = FALSE;
		for (rules_p rule = non_term->recursive; rule != NULL; rule = rule->next)
		{
			DECL_RESULT(start_result)
			if (rule->rec_start_function != NULL)
			{
				if (!rule->rec_start_function(result, &start_result))
				{
					DISP_RESULT(start_result)
					continue;
				}
			}
			DECL_RESULT(rule_result)
			if (parse_rule(parser, rule->elements, &start_result, rule, &rule_result))
			{
				parsed_a_rule = TRUE;
				result_assign(result, &rule_result);
				DISP_RESULT(rule_result)
				DISP_RESULT(start_result)
				break;
			}
			DISP_RESULT(rule_result)
			DISP_RESULT(start_result)
		}
	}

	DEBUG_EXIT_P1("parse_nt(%s) = ", nt);
	DEBUG_PT(result); DEBUG_NL;
	if (debug_nt)
	{   depth -= 2;
		printf("%*.*s", depth, depth, "");
		printf("Parsed: %s\n", nt);
	}
	
	/* Restore current non-terminal to its previous value */
	parser->current_nt = surr_nt;
	
	/* Update the cache item, if available */
	if (cache_item != NULL)
	{
		result_assign(&cache_item->result, result);
		cache_item->success = s_success;
		cache_item->next_pos = parser->text_buffer->pos;
	}
	return TRUE;
}

/*
	Parsing a rule
	~~~~~~~~~~~~~~
	
	This function is called to parse (the remainder of) a rule. If it fails,
	the current position in text buffer is reset to the position it was at
	the start of the call. This function first tries to parse the first
	element of the rule. If this is succeeds, the function will be called
	recursively for the rest of the rule.
	
*/

bool parse_element(parser_p parser, element_p element, const result_p prev_result, result_p result);
bool parse_seq(parser_p parser, element_p element, const result_p prev_seq, const result_p prev, rules_p rule, result_p result);

bool parse_rule(parser_p parser, element_p element, const result_p prev_result, rules_p rule, result_p rule_result)
{
	DEBUG_ENTER("parse_rule: ");
	DEBUG_PR(element); DEBUG_NL;

	if (element == NULL)
	{
		/* At the end of the rule: */
		if (rule->end_function == 0)
			result_assign(rule_result, prev_result);
		else if (!rule->end_function(prev_result, rule->end_function_data, rule_result))
		{
			DEBUG_EXIT("parse_rule failed by end function "); DEBUG_NL;
			return FALSE;
		}
		DEBUG_EXIT("parse_rule = ");
		DEBUG_PT(rule_result); DEBUG_NL;
		return TRUE;
	}

	/* If the first element is optional and should be avoided, first an attempt
	   will be made to skip the element and parse the remainder of the rule */
	if (element->optional && element->avoid)
	{
		/* If a add skip function is defined, apply it. (An add skip function
		   can be used to process the absence of the element with the result.)
		   Otherwise, if a add function is defined, it will be called with an
		   'empty' result, signaling that no element was parsed.
		   Otherwise, the previous result is used. */
		DECL_RESULT(skip_result);
		if (element->add_skip_function != NULL)
		{
			if (!element->add_skip_function(prev_result, &skip_result))
			{
				DISP_RESULT(skip_result);
	            DEBUG_EXIT("parse_rule failed due to add skip function"); DEBUG_NL;
				return FALSE;
			}
		}
		else if (element->add_function != NULL)
		{
			DECL_RESULT(empty);
			if (!element->add_function(prev_result, &empty, &skip_result))
			{
				DISP_RESULT(empty);
				DISP_RESULT(skip_result);
	            DEBUG_EXIT("parse_rule failed due to add function"); DEBUG_NL;
				return FALSE;
			}
			DISP_RESULT(empty);
		}
		else
			result_assign(&skip_result, prev_result);
			
		if (parse_rule(parser, element->next, &skip_result, rule, rule_result))
		{
			DISP_RESULT(skip_result);
            DEBUG_EXIT("parse_rule = ");
            DEBUG_PT(rule_result); DEBUG_NL;
			return TRUE;
		}
		DISP_RESULT(skip_result);
	}
		
	/* Store the current position */
	text_pos_t sp = parser->text_buffer->pos;
	
	DECL_RESULT(part_result);
	if (element->sequence)
	{
		/* The first element of the fule is a sequence. */
		DECL_RESULT(seq_begin);
		if (element->begin_seq_function != NULL)
			element->begin_seq_function(prev_result, &seq_begin);
		
		/* Try to parse the first element of the sequence. */
		DECL_RESULT(seq_elem);
		if (parse_element(parser, element, &seq_begin, &seq_elem))
		{
			/* Now parse the remainder elements of the sequence (and thereafter the remainder of the rule. */
			if (parse_seq(parser, element, &seq_elem, prev_result, rule, rule_result))
			{
				DISP_RESULT(seq_elem);
				DISP_RESULT(seq_begin);
				DEBUG_EXIT("parse_rule = ");
				DEBUG_PT(rule_result); DEBUG_NL;
				return TRUE;
			}
		}
		DISP_RESULT(seq_begin);
		DISP_RESULT(seq_elem);
	}
	else
	{
		/* The first element is not a sequence: Try to parse the first element */
		DECL_RESULT(elem);
		if (parse_element(parser, element, prev_result, &elem))
		{
			if (parse_rule(parser, element->next, &elem, rule, rule_result))
			{
				DISP_RESULT(elem);
				DEBUG_EXIT("parse_rule = ");
				DEBUG_PT(rule_result); DEBUG_NL;
				return TRUE;
			}
		}
		DISP_RESULT(elem);
	}
	
	/* Failed to parse the rule: reset the current position to the saved position. */
	text_buffer_set_pos(parser->text_buffer, &sp);
	
	/* If the element was optional (and should not be avoided): Skip the element
	   and try to parse the remainder of the rule */
	if (element->optional && !element->avoid)
	{
		DECL_RESULT(skip_result);
		if (element->add_skip_function != NULL)
		{
			if (!element->add_skip_function(prev_result, &skip_result))
			{
				DISP_RESULT(skip_result);
	            DEBUG_EXIT("parse_rule failed due to add skip function"); DEBUG_NL;
				return FALSE;
			}
		}
		else if (element->add_function != NULL)
		{
			DECL_RESULT(empty);
			if (!element->add_function(prev_result, &empty, &skip_result))
			{
				DISP_RESULT(empty);
				DISP_RESULT(skip_result);
	            DEBUG_EXIT("parse_rule failed due to add function"); DEBUG_NL;
				return FALSE;
			}
			DISP_RESULT(empty);
		}
		else
			result_assign(&skip_result, prev_result);
			
		if (parse_rule(parser, element->next, &skip_result, rule, rule_result))
		{
			DISP_RESULT(skip_result);
            DEBUG_EXIT("parse_rule = ");
            DEBUG_PT(rule_result); DEBUG_NL;
			return TRUE;
		}
		DISP_RESULT(skip_result);
	}

    DEBUG_EXIT("parse_rule: failed"); DEBUG_NL;
	return FALSE;
}

bool parse_seq(parser_p parser, element_p element, const result_p prev_seq, const result_p prev, rules_p rule, result_p rule_result)
{
	/* In case of the avoid modifier, first an attempt is made to parse the
	   remained of the rule */
	if (element->avoid)
	{
		DECL_RESULT(result);
		if (element->add_seq_function != NULL && !element->add_seq_function(prev, prev_seq, &result))
		{
			DISP_RESULT(result);
			return FALSE;
		}
		if (parse_rule(parser, element->next, &result, rule, rule_result))
		{
			DISP_RESULT(result);
			return TRUE;
		}
		DISP_RESULT(result);
	}
	
	/* Store the current position */
	text_pos_t sp = parser->text_buffer->pos;

	/* If a chain rule is defined, try to parse it.*/
	if (element->chain_rule != NULL)
	{
		DECL_RESULT(dummy_chain_elem);
		if (parse_rule(parser, element->chain_rule, NULL, NULL, &dummy_chain_elem))
		{
			/* If the chain rule was succesful, an element should follow. */
			DISP_RESULT(dummy_chain_elem);
			DECL_RESULT(seq_elem);
			if (parse_element(parser, element, prev_seq, &seq_elem))
			{
				/* If succesful, try to parse the remainder of the sequence (and thereafter the remainder of the rule) */
				if (parse_seq(parser, element, &seq_elem, prev, rule, rule_result))
				{
					DISP_RESULT(seq_elem);
					return TRUE;
				}
			}
			DISP_RESULT(seq_elem);
			return FALSE;
		}
		DISP_RESULT(dummy_chain_elem);
	}
	else
	{
		/* Try to parse the next element of the sequence */
		DECL_RESULT(seq_elem);
		if (parse_element(parser, element, prev_seq, &seq_elem))
		{
			/* If succesful, try to parse the remainder of the sequence (and thereafter the remainder of the rule) */
			if (parse_seq(parser, element, &seq_elem, prev, rule, rule_result))
			{
				DISP_RESULT(seq_elem);
				return TRUE;
			}
		}
		DISP_RESULT(seq_elem);
	}
	
	/* Failed to parse the next element of the sequence: reset the current position to the saved position. */
	text_buffer_set_pos(parser->text_buffer, &sp);

	/* In case of the avoid modifier, an attempt to parse the remained of the
	   rule, was already made. So, only in case of no avoid modifier, attempt
	   to parse the remainder of the rule */
	if (!element->avoid)
	{
		DECL_RESULT(result);
		if (element->add_seq_function != NULL && !element->add_seq_function(prev, prev_seq, &result))
		{
			DISP_RESULT(result);
			return FALSE;
		}
		
		if (parse_rule(parser, element->next, &result, rule, rule_result))
		{
			DISP_RESULT(result);
			return TRUE;
		}
		DISP_RESULT(result);
	}
	
	return FALSE;
}


/*
	Parse an element
	~~~~~~~~~~~~~~~~
	
	The following function is used to parse a part of an element, not dealing
	with if the element is optional or a sequence.
*/

bool parse_element(parser_p parser, element_p element, const result_p prev_result, result_p result)
{
	/* Store the current position */
	text_pos_t sp = parser->text_buffer->pos;

	switch( element->kind )
	{
		case rk_nt:
			{
				/* Parse the non-terminal */
				DECL_RESULT(nt_result)
				if (!parse_nt(parser, element->info.non_terminal, &nt_result))
				{
					DISP_RESULT(nt_result)
					return FALSE;
				}
				
				/* If there is a condition, evaluate the result */
				if (element->condition != 0 && !(*element->condition)(&nt_result, element->condition_argument))
				{
					DISP_RESULT(nt_result)
					text_buffer_set_pos(parser->text_buffer, &sp);
					return FALSE;
				}
				
				/* Combine the result with the previous result */
				if (element->add_function == 0)
					result_assign(result, prev_result);
				else if (!(*element->add_function)(prev_result, &nt_result, result))
				{
					DISP_RESULT(nt_result)
					text_buffer_set_pos(parser->text_buffer, &sp);
					return FALSE;
				}
				DISP_RESULT(nt_result)
			}
			break;
		case rk_grouping:
			{
				/* Try all rules in the grouping */
				DECL_RESULT(rule_result);
				rules_p rule = element->info.rules;
				for ( ; rule != NULL; rule = rule->next )
				{
					DECL_RESULT(start);
					result_assign(&start, prev_result);
					if (parse_rule(parser, rule->elements, &start, rule, &rule_result))
					{
						DISP_RESULT(start);
						break;
					}
					DISP_RESULT(start);
				}
				if (rule == NULL)
				{
					/* Non of the rules worked */
					DISP_RESULT(rule_result)
					return FALSE;
				}
				
				/* Combine the result of the rule with the previous result */
				if (element->add_function == 0)
					result_assign(result, &rule_result);
				else if (!(*element->add_function)(prev_result, &rule_result, result))
				{
					DISP_RESULT(rule_result)
					text_buffer_set_pos(parser->text_buffer, &sp);
					return FALSE;
				}
				DISP_RESULT(rule_result)
			}
			break;
		case rk_end:
			/* Check if the end of the buffer is reached */
			if (!text_buffer_end(parser->text_buffer))
				return FALSE;
			result_assign(result, prev_result);
			break;
		case rk_char:
			/* Check if the specified character is found at the current position in the text buffer */
			if (*parser->text_buffer->info != element->info.ch)
				return FALSE;
			/* Advance the current position of the text buffer */
			text_buffer_next(parser->text_buffer);
			/* Process the character */
			if (element->add_char_function == 0)
				result_assign(result, prev_result);
			else if (!(*element->add_char_function)(prev_result, element->info.ch, result))
				return FALSE;
			break;
		case rk_charset:
			/* Check if the character at the current position in the text buffer is found in the character set */
			if (!char_set_contains(element->info.char_set, *parser->text_buffer->info))
				return FALSE;
			{
				/* Remember the character and advance the current position of the text buffer */
				char ch = *parser->text_buffer->info;
				text_buffer_next(parser->text_buffer);
				/* Process the character */
				if (element->add_char_function == 0)
					result_assign(result, prev_result);
				else if (!(*element->add_char_function)(prev_result, ch, result))
					return FALSE;
			}
			break;
		case rk_term:
			/* Call the terminal parse function and see if it has parsed something */
			{
				const char *next_pos = element->info.terminal_function(parser->text_buffer->info, result);
				/* If the start position is returned, assume that it failed. */
				if (next_pos <= parser->text_buffer->info)
					return FALSE;
				/* Increment the buffer till the returned position */
				while (parser->text_buffer->info < next_pos)
					text_buffer_next(parser->text_buffer);
			}
			break;
		default:
			return FALSE;
			break;
	}
	
	/* Set the position on the result */
	if (element->set_pos != NULL)
		element->set_pos(result, &sp);

	return TRUE;
}


/*
	Brute force cache
	~~~~~~~~~~~~~~~~~
	
	A simple cache implementation, is one that simply stores all results for
	all positions in the input text.

*/

typedef struct solution *solution_p;
struct solution
{
	cache_item_t cache_item;
	const char *nt;
	solution_p next;
};
typedef struct
{
	solution_p *sols;        /* Array of solutions at locations */
	size_t len;              /* Length of array (equal to length of input) */
} solutions_t, *solutions_p;


void solutions_init(solutions_p solutions, text_buffer_p text_buffer)
{
    solutions->len = text_buffer->buffer_len;
	solutions->sols = MALLOC_N(solutions->len+1, solution_p);
	size_t i;
	for (i = 0; i < solutions->len+1; i++)
		solutions->sols[i] = NULL;
}

void solutions_free(solutions_p solutions)
{
	size_t i;
	for (i = 0; i < solutions->len+1; i++)
	{	solution_p sol = solutions->sols[i];

		while (sol != NULL)
		{	if (sol->cache_item.result.dec != 0)
		    	sol->cache_item.result.dec(sol->cache_item.result.data);
			solution_p next_sol = sol->next;
		    FREE(sol);
			sol = next_sol;
		}
  	}
	FREE(solutions->sols);
}

cache_item_p solutions_find(void *cache, size_t pos, const char *nt)
{
	solutions_p solutions = (solutions_p)cache;
	solution_p sol;

	if (pos > solutions->len)
		pos = solutions->len;

	for (sol = solutions->sols[pos]; sol != NULL; sol = sol->next)
		if (sol->nt == nt)
		 	return &sol->cache_item;

	sol = MALLOC(struct solution);
	sol->next = solutions->sols[pos];
	sol->nt = nt;
	sol->cache_item.success = s_unknown;
	result_init(&sol->cache_item.result);
	solutions->sols[pos] = sol;
	return &sol->cache_item;
}

/*
	White space tests
	~~~~~~~~~~~~~~~~~
*/

void test_parse_white_space(non_terminal_dict_p *all_nt, const char *input)
{
	text_buffer_t text_buffer;
	text_buffer_assign_string(&text_buffer, input);
	
	solutions_t solutions;
	solutions_init(&solutions, &text_buffer);
	
	parser_t parser;
	parser_init(&parser, &text_buffer);
	parser.cache_hit_function = solutions_find;
	parser.cache = &solutions;
	
	DECL_RESULT(result);
	if (parse_nt(&parser, find_nt("white_space", all_nt), &result) && text_buffer_end(&text_buffer))
	{
		fprintf(stderr, "OK: parsed white space\n");
	}
	else
	{
		fprintf(stderr, "ERROR: failed to parse white space from '%s'\n", input);
	}
	DISP_RESULT(result);

	solutions_free(&solutions);
}

void test_white_space_grammar(non_terminal_dict_p *all_nt)
{
	test_parse_white_space(all_nt, " ");
	test_parse_white_space(all_nt, "/* */");
}

/*
	Number tests
	~~~~~~~~~~~~
*/

void test_parse_number(non_terminal_dict_p *all_nt, const char *input, int num)
{
	text_buffer_t text_buffer;
	text_buffer_assign_string(&text_buffer, input);
	
	solutions_t solutions;
	solutions_init(&solutions, &text_buffer);
	
	parser_t parser;
	parser_init(&parser, &text_buffer);
	parser.cache_hit_function = solutions_find;
	parser.cache = &solutions;
	
	DECL_RESULT(result);
	if (parse_nt(&parser, find_nt("number", all_nt), &result) && text_buffer_end(&text_buffer))
	{
		if (result.data == NULL)
			fprintf(stderr, "ERROR: parsing '%s' did not return result\n", input);
		else if (((number_data_p)result.data)->num != num)
			fprintf(stderr, "ERROR: parsed value %ld from '%s' instead of expected %d\n",
				((number_data_p)result.data)->num, input, num);
		else
			fprintf(stderr, "OK: parsed value %ld from '%s'\n", ((number_data_p)result.data)->num, input);
	}
	else
		fprintf(stderr, "ERROR: failed to parse number from '%s'\n", input);
	DISP_RESULT(result);

	solutions_free(&solutions);
}

void test_number_grammar(non_terminal_dict_p *all_nt)
{
	test_parse_number(all_nt, "0", 0);
	test_parse_number(all_nt, "123", 123);
}

/*
	Abstract Syntax Tree
	~~~~~~~~~~~~~~~~~~~~
	The following section of the code implements a representation for
	Abstract Syntax Trees.
*/

typedef struct
{
	ref_counted_base_t _base;
	const char *type_name;
	unsigned int line;
	unsigned int column;
} tree_node_t, *tree_node_p;

void init_tree_node(tree_node_p tree_node, const char *type_name, void (*release_node)(void *))
{
	tree_node->_base.cnt = 1;
	tree_node->_base.release = release_node;
	tree_node->type_name = type_name;
	tree_node->line = 0;
	tree_node->column = 0;
}

void tree_node_set_pos(tree_node_p tree_node, text_pos_p ps)
{
	tree_node->line = ps->cur_line;
	tree_node->column = ps->cur_column;
}

typedef struct tree_t *tree_p;
struct tree_t
{
	tree_node_t _node;
	unsigned int nr_children;
	result_t *children;
};

tree_p old_trees = NULL;
long alloced_trees = 0L;

void release_tree(void *data)
{
	tree_p tree = (tree_p)data;;

	alloced_trees--;

	if (tree->nr_children > 0)
	{
		for (int i = 0; i < tree->nr_children; i++)
			result_release(&tree->children[i]);
		free(tree->children);
	}
	*(tree_p*)tree = old_trees;
	old_trees = tree;
}

tree_p malloc_tree(const char *name)
{
	tree_p new_tree;

	if (old_trees)
	{   new_tree = old_trees;
		old_trees = *(tree_p*)old_trees;
	}
	else
		new_tree = MALLOC(struct tree_t);

	init_tree_node(&new_tree->_node, name, release_tree);
	new_tree->nr_children = 0;
	new_tree->children = NULL;
	
	alloced_trees++;

	return new_tree;
}

typedef struct prev_child_t *prev_child_p;
struct prev_child_t
{
	ref_counted_base_t _base;
	prev_child_p prev;
	result_t child;
};

prev_child_p old_prev_child = NULL;

void release_prev_child( void *data )
{
	prev_child_p prev_child = (prev_child_p)data;
	result_release(&prev_child->child);
	FREE(prev_child);
}

prev_child_p malloc_prev_child()
{
	prev_child_p new_prev_child = MALLOC(struct prev_child_t);
	new_prev_child->_base.cnt = 1;
	new_prev_child->_base.release = release_prev_child;
	result_init(&new_prev_child->child);
	new_prev_child->prev = NULL;
	return new_prev_child;
}

bool add_child(result_p prev, result_p elem, result_p result)
{
	prev_child_p new_prev_child = malloc_prev_child();
	new_prev_child->prev = (prev_child_p)prev->data;
	result_assign(&new_prev_child->child, elem);
	result_assign_ref_counted(result, new_prev_child, NULL);
	return TRUE;
}

bool rec_add_child(result_p rec_result, result_p result)
{
	prev_child_p new_prev_child = malloc_prev_child();
	new_prev_child->prev = NULL;
	result_assign(&new_prev_child->child, rec_result);
	result_assign_ref_counted(result, new_prev_child, NULL);
	return TRUE;
}

bool take_child(result_p prev, result_p elem, result_p result)
{
	result_assign(result, elem);
	return TRUE;
}

tree_p make_tree_with_children(const char* name, prev_child_p children)
{
	tree_p tree = malloc_tree(name);
	prev_child_p child;
	int i = 0;
	for (child = children; child != NULL; child = child->prev)
		i++;
	tree->nr_children = i;
	tree->children = MALLOC_N(tree->nr_children, result_t);
	for (child = children; child != NULL; child = child->prev)
	{
		i--;
		result_init(&tree->children[i]);
		result_assign(&tree->children[i], &child->child);
	}
	return tree;
}

void tree_print(void *data, ostream_p ostream)
{
	tree_p tree = (tree_p)data;
	if (tree->_node.type_name != NULL)
		ostream_puts(ostream, tree->_node.type_name);
	ostream_put(ostream, '(');
	for (int i = 0; i < tree->nr_children; i++)
	{
		if (i > 0)
			ostream_put(ostream, ',');
		result_print(&tree->children[i], ostream);
	}
	ostream_put(ostream, ')');
}

bool make_tree(const result_p rule_result, void* data, result_p result)
{
	prev_child_p children = (prev_child_p)rule_result->data;
	const char *name = (const char*)data;
	tree_p tree = make_tree_with_children(name, children);
	result_assign_ref_counted(result, tree, tree_print);
	return TRUE;
}

bool pass_tree(const result_p rule_result, void* data, result_p result)
{
	prev_child_p child = (prev_child_p)rule_result->data;
	result_transfer(result, &child->child);
	return TRUE;
}


/*
	Keywords
	~~~~~~~~
	Many programming languages have keywords, which have the same lexical
	catagory as identifiers. This means we need some function to test
	whether an identifier is equal to one of the keywords. One way to
	do this is to use hexadecimal hash tree. This can also be used to map
	every identifier to a unique pointer, such that comparing two identifiers
	can simply be done by comparing the two pointers.

	A hexadecimal hash tree
	~~~~~~~~~~~~~~~~~~~~~~~	
	The following structure implements a mapping of strings to an integer
	value in the range [0..254]. It is a tree of hashs in combination with
	a very fast incremental hash function. In this way, it tries to combine
	the benefits of trees and hashs. The incremental hash function will first
	return the lower 4 bits of the characters in the string, and following
	this the higher 4 bits of the characters.
*/

typedef struct hexa_hash_tree_t hexa_hash_tree_t, *hexa_hash_tree_p;

struct hexa_hash_tree_t
{	byte state;
	union
	{	char *string;
		hexa_hash_tree_p *children;
	} data;
};

byte *keyword_state = NULL;

char *ident_string(char *s)
/*  Returns a unique address representing the string. the global
    keyword_state will point to the integer value in the range [0..254].
	If the string does not occure in the store, it is added and the state
	is initialized with 0.
*/
{
	static hexa_hash_tree_p hash_tree = NULL;
	hexa_hash_tree_p *r_node = &hash_tree;
	char *vs = s;
	int depth;
	int mode = 0;

	for (depth = 0; ; depth++)
	{   hexa_hash_tree_p node = *r_node;

		if (node == NULL)
		{   node = MALLOC(hexa_hash_tree_t);
			node->state = 0;
			STRCPY(node->data.string, s);
			*r_node = node;
			keyword_state = &node->state;
			return node->data.string;
		}

		if (node->state != 255)
		{   char *cs = node->data.string;
			hexa_hash_tree_p *children;
			unsigned short i, v = 0;

			if (*cs == *s && strcmp(cs+1, s+1) == 0)
			{   keyword_state = &node->state;
				return node->data.string;
			}

			children = MALLOC_N(16, hexa_hash_tree_t*);
			for (i = 0; i < 16; i++)
				children[i] = NULL;

			i = strlen(cs);
			if (depth <= i)
				v = ((byte)cs[depth]) & 15;
			else if (depth <= i*2)
				v = ((byte)cs[depth-i-1]) >> 4;

			children[v] = node;

			node = MALLOC(hexa_hash_tree_t);
			node->state = 255;
			node->data.children = children;
			*r_node = node;
		}
		{   unsigned short v;
			if (*vs == '\0')
			{   v = 0;
				if (mode == 0)
				{   mode = 1;
					vs = s;
				}
			}
			else if (mode == 0)
				v = ((unsigned short)*vs++) & 15;
			else
				v = ((unsigned short)*vs++) >> 4;

			r_node = &node->data.children[v];
		}
	}
}

/*  Parsing an identifier  */

/*  Data structure needed during parsing.
    Only the first 64 characters of the identifier will be significant. */

typedef struct ident_data
{
	ref_counted_base_t _base;
	char ident[65];
	int len;
	text_pos_t ps;
} *ident_data_p;

bool ident_add_char(result_p prev, char ch, result_p result)
{
	if (prev->data == NULL)
	{
		ident_data_p ident_data = MALLOC(struct ident_data);
		ident_data->_base.release = NULL;
		result_assign_ref_counted(result, ident_data, NULL);
		ident_data->ident[0] = ch;
		ident_data->len = 1;
	}
	else
	{
		result_assign(result, prev);
		ident_data_p ident_data = (ident_data_p)result->data;
		if (ident_data->len < 64)
			ident_data->ident[ident_data->len++] = ch;
	}
	return TRUE;
}

void ident_set_pos(result_p result, text_pos_p ps)
{
	if (result->data != 0)
		((ident_data_p)result->data)->ps = *ps;
}

void pass_to_sequence(result_p prev, result_p seq)
{
	result_assign(seq, prev);
}

/*  Ident tree node structure */

typedef struct ident_t *ident_p;
struct ident_t
{
	tree_node_t _node;
	const char *name;
};

void ident_print(void *data, ostream_p ostream)
{
	ostream_puts(ostream, ((ident_p)data)->name);
}
const char *ident_type = "ident";

bool create_ident_tree(const result_p rule_result, void* data, result_p result)
{
	ident_data_p ident_data = (ident_data_p)rule_result->data;
	if (ident_data == 0)
	{
		fprintf(stderr, "NULL\n");
		return TRUE;
	}
	ident_data->ident[ident_data->len] = '\0';
	ident_p ident = MALLOC(struct ident_t);
	init_tree_node(&ident->_node, ident_type, NULL);
	tree_node_set_pos(&ident->_node, &ident_data->ps);
	ident->name = ident_string(ident_data->ident);
	result_assign_ref_counted(result, ident, ident_print);
	return TRUE;
}

/*  Ident grammar  */

void ident_grammar(non_terminal_dict_p *all_nt)
{
	HEADER(all_nt)
	
	NT_DEF("ident")
		RULE
			CHARSET(ident_add_char) ADD_RANGE('a', 'z') ADD_RANGE('A', 'Z') ADD_CHAR('_') SET_PS(ident_set_pos)
			CHARSET(ident_add_char) ADD_RANGE('a', 'z') ADD_RANGE('A', 'Z') ADD_CHAR('_') ADD_RANGE('0', '9') SEQ(pass_to_sequence, use_sequence_result) OPT(0)
			END_FUNCTION(create_ident_tree)
}

/*
	Ident tests
	~~~~~~~~~~~
*/

void test_parse_ident(non_terminal_dict_p *all_nt, const char *input)
{
	text_buffer_t text_buffer;
	text_buffer_assign_string(&text_buffer, input);
	
	solutions_t solutions;
	solutions_init(&solutions, &text_buffer);
	
	parser_t parser;
	parser_init(&parser, &text_buffer);
	parser.cache_hit_function = solutions_find;
	parser.cache = &solutions;
	
	DECL_RESULT(result);
	if (parse_nt(&parser, find_nt("ident", all_nt), &result) && text_buffer_end(&text_buffer))
	{
		if (result.data == NULL)
			fprintf(stderr, "ERROR: parsing '%s' did not return result\n", input);
		else
		{
			tree_node_p tree_node = (tree_node_p)result.data;
			if (tree_node->line != 1 && tree_node->column != 1)
				fprintf(stderr, "WARNING: tree node position %d:%d is not 1:1\n", tree_node->line, tree_node->column);
			if (tree_node->type_name != ident_type)
				fprintf(stderr, "ERROR: tree node is not of type ident_type\n");
			else
			{
				ident_p ident = (ident_p)tree_node;
				if (strcmp(ident->name, input) != 0)
					fprintf(stderr, "ERROR: parsed value '%s' from '%s' instead of expected '%s'\n",
					ident->name, input, input);
				else
					fprintf(stderr, "OK: parsed ident '%s' from '%s'\n", ident->name, input);
			}
		}
	}
	else
	{
		fprintf(stderr, "ERROR: failed to parse ident from '%s'\n", input);
	}
	DISP_RESULT(result);
	
	solutions_free(&solutions);
}

void test_ident_grammar(non_terminal_dict_p *all_nt)
{
	test_parse_ident(all_nt, "aBc");
	test_parse_ident(all_nt, "_123");
}

/*
	Char result
	~~~~~~~~~~~

	The struct for representing the char has but a single member
	(besides the member for the reference counting base).
*/

typedef struct char_data
{
	ref_counted_base_t _base;
	char ch;
	text_pos_t ps;
} *char_data_p;

void print_single_char(char ch, ostream_p ostream)
{
	if (ch == '\0')
		ostream_puts(ostream, "\\0");
	else if (ch == '\'')
		ostream_puts(ostream, "\\'");
	else if (ch == '\n')
		ostream_puts(ostream, "\\n");
	else
		ostream_put(ostream, ch);
}

void char_data_print(void *data, ostream_p ostream)
{
	ostream_puts(ostream, "char '");
	print_single_char(((char_data_p)data)->ch, ostream);
	ostream_puts(ostream, "'");
}

void char_set_pos(result_p result, text_pos_p ps)
{
	char_data_p char_data = MALLOC(struct char_data);
	char_data->ps = *ps;
	char_data->_base.release = 0;
	result_assign_ref_counted(result, char_data, char_data_print);
}

bool escaped_char(result_p prev, char ch, result_p result)
{
	result_assign(result, prev);
	((char_data_p)result->data)->ch = ch == '0' ? '\0' : ch == 'n' ? '\n' : ch;
	return TRUE;
}

bool normal_char(result_p prev, char ch, result_p result)
{
	result_assign(result, prev);
	((char_data_p)result->data)->ch = ch;
	return TRUE;
}

/*	Char tree node structure */

typedef struct char_node_t *char_node_p;
struct char_node_t
{
	tree_node_t _node;
	char ch;
};

const char *char_node_type = "char";

void char_node_print(void *data, ostream_p ostream)
{
	ostream_puts(ostream, "char '");
	print_single_char(((char_node_p)data)->ch, ostream);
	ostream_puts(ostream, "'");
}

bool create_char_tree(const result_p rule_result, void* data, result_p result)
{
	char_data_p char_data = (char_data_p)rule_result->data;

	char_node_p char_node = MALLOC(struct char_node_t);
	init_tree_node(&char_node->_node, char_node_type, NULL);
	tree_node_set_pos(&char_node->_node, &char_data->ps);
	char_node->ch = char_data->ch;
	result_assign_ref_counted(result, char_node, char_node_print);
	return TRUE;
}

/*  Char grammar  */

void char_grammar(non_terminal_dict_p *all_nt)
{
	HEADER(all_nt)
	
	NT_DEF("char")
		RULE
			CHAR('\'') SET_PS(char_set_pos)
			{ GROUPING
				RULE // Escaped character
					CHAR('\\') CHARSET(escaped_char) ADD_CHAR('0') ADD_CHAR('\'') ADD_CHAR('\\') ADD_CHAR('n')
				RULE // Normal character
					CHARSET(normal_char) ADD_RANGE(' ', 126) REMOVE_CHAR('\\') REMOVE_CHAR('\'')
			}
			CHAR('\'')
			END_FUNCTION(create_char_tree)
}

/*
	Char tests
	~~~~~~~~~~
*/

void test_parse_char(non_terminal_dict_p *all_nt, const char *input, char ch)
{
	text_buffer_t text_buffer;
	text_buffer_assign_string(&text_buffer, input);
	
	solutions_t solutions;
	solutions_init(&solutions, &text_buffer);
	
	parser_t parser;
	parser_init(&parser, &text_buffer);
	parser.cache_hit_function = solutions_find;
	parser.cache = &solutions;
	
	DECL_RESULT(result);
	if (parse_nt(&parser, find_nt("char", all_nt), &result) && text_buffer_end(&text_buffer))
	{
		if (result.data == NULL)
			fprintf(stderr, "ERROR: parsing '%s' did not return result\n", input);
		else
		{
			tree_node_p tree_node = (tree_node_p)result.data;
			if (tree_node->line != 1 && tree_node->column != 1)
				fprintf(stderr, "WARNING: tree node position %d:%d is not 1:1\n", tree_node->line, tree_node->column);
			if (tree_node->type_name != char_node_type)
				fprintf(stderr, "ERROR: tree node is not of type char_node_type\n");
			else
			{
				char_node_p char_node = (char_node_p)tree_node;
				if (char_node->ch != ch)
					fprintf(stderr, "ERROR: parsed value '%c' from '%s' instead of expected '%c'\n",
					char_node->ch, input, ch);
				else
					fprintf(stderr, "OK: parsed char %d from '%s'\n", char_node->ch, input);
			}
		}
	}
	else
	{
		fprintf(stderr, "ERROR: failed to parse char from '%s'\n", input);
	}
	DISP_RESULT(result);
	
	solutions_free(&solutions);
}

void test_char_grammar(non_terminal_dict_p *all_nt)
{
	test_parse_char(all_nt, "'c'", 'c');
	test_parse_char(all_nt, "'\\0'", '\0');
	test_parse_char(all_nt, "'\\''", '\'');
	test_parse_char(all_nt, "'\\\\'", '\\');
	test_parse_char(all_nt, "'\\n'", '\n');
}



bool equal_string(result_p result, const void *argument)
{
	const char *keyword_name = (const char*)argument;
	return    result->data != NULL
		   && ((tree_node_p)result->data)->type_name == ident_type
		   && strcmp(((ident_p)result->data)->name, keyword_name) == 0;
}

bool not_a_keyword(result_p result, const void *argument)
{
	return *keyword_state == 0;
}

const char* list_type = "list";

bool add_seq_as_list(result_p prev, result_p seq, result_p result)
{
	prev_child_p new_prev_child = malloc_prev_child();
	new_prev_child->prev = (prev_child_p)prev->data;
	tree_p list = make_tree_with_children(list_type, (prev_child_p)seq->data);
	result_assign_ref_counted(&new_prev_child->child, list, tree_print);
	result_assign_ref_counted(result, new_prev_child, NULL);
	return TRUE;
}

#define NT(S) NTF(S, add_child)
#define NTP(S) NTF(S, take_child)
#define WS NTF("white_space", 0)
#define PASS rules->end_function = pass_tree;
#define TREE(N) rules->end_function = make_tree; rules->end_function_data = N;
#define KEYWORD(K) NTF("ident", 0) element->condition = equal_string; element->condition_argument = ident_string(K); *keyword_state = 1; WS
#define OPTN OPT(0)
#define IDENT NTF("ident", add_child) element->condition = not_a_keyword; WS
#define SEQL SEQ(0, add_seq_as_list)
#define REC_RULEC REC_RULE(rec_add_child);

void c_grammar(non_terminal_dict_p *all_nt)
{
	white_space_grammar(all_nt);
	ident_grammar(all_nt);
	
	HEADER(all_nt)
	
	NT_DEF("primary_expr")
		RULE IDENT PASS
		RULE NTP("int")
		RULE NTP("double")
		RULE NTP("char")
		RULE NTP("string")
		RULE CHAR('(') WS NTP("expr") CHAR(')') WS

	NT_DEF("postfix_expr")
		RULE NTP("primary_expr")
		REC_RULEC CHAR('[') WS NT("expr") CHAR(']') TREE("arrayexp")
		REC_RULEC CHAR('(') WS NT("assignment_expr") SEQL { CHAIN CHAR(',') WS } OPTN CHAR(')') TREE("call")
		REC_RULEC CHAR('.') WS IDENT TREE("field")
		REC_RULEC CHAR('-') CHAR('>') WS IDENT TREE("fieldderef")
		REC_RULEC CHAR('+') CHAR('+') WS TREE("post_inc")
		REC_RULEC CHAR('-') CHAR('-') WS TREE("post_dec")

	NT_DEF("unary_expr")
		RULE CHAR('+') CHAR('+') WS NT("unary_expr") TREE("pre_inc")
		RULE CHAR('-') CHAR('-') WS NT("unary_expr") TREE("pre_dec")
		RULE CHAR('&') WS NT("cast_expr") TREE("address_of")
		RULE CHAR('*') WS NT("cast_expr") TREE("deref")
		RULE CHAR('+') WS NT("cast_expr") TREE("plus")
		RULE CHAR('-') WS NT("cast_expr") TREE("min")
		RULE CHAR('~') WS NT("cast_expr") TREE("invert")
		RULE CHAR('!') WS NT("cast_expr") TREE("not")
		RULE KEYWORD("sizeof")
		{ GROUPING
			RULE NT("unary_expr") TREE("typeof")
			RULE CHAR('(') WS IDENT CHAR(')') WS
		} TREE("sizeof")
		RULE NTP("postfix_expr")

	NT_DEF("cast_expr")
		RULE CHAR('(') WS NT("abstract_declaration") CHAR(')') WS NT("cast_expr") TREE("cast")
		RULE NTP("unary_expr")

	NT_DEF("l_expr1")
		RULE NTP("cast_expr")
		REC_RULEC WS CHAR('*') WS NT("cast_expr") TREE("times")
		REC_RULEC WS CHAR('/') WS NT("cast_expr") TREE("div")
		REC_RULEC WS CHAR('%') WS NT("cast_expr") TREE("mod")

	NT_DEF("l_expr2")
		RULE NTP("l_expr1")
		REC_RULEC WS CHAR('+') WS NT("l_expr1") TREE("add")
		REC_RULEC WS CHAR('-') WS NT("l_expr1") TREE("sub")

	NT_DEF("l_expr3")
		RULE NTP("l_expr2")
		REC_RULEC WS CHAR('<') CHAR('<') WS NT("l_expr2") TREE("ls")
		REC_RULEC WS CHAR('>') CHAR('>') WS NT("l_expr2") TREE("rs")

	NT_DEF("l_expr4")
		RULE NTP("l_expr3")
		REC_RULEC WS CHAR('<') CHAR('=') WS NT("l_expr3") TREE("le")
		REC_RULEC WS CHAR('>') CHAR('=') WS NT("l_expr3") TREE("ge")
		REC_RULEC WS CHAR('<') WS NT("l_expr3") TREE("lt")
		REC_RULEC WS CHAR('>') WS NT("l_expr3") TREE("gt")
		REC_RULEC WS CHAR('=') CHAR('=') WS NT("l_expr3") TREE("eq")
		REC_RULEC WS CHAR('!') CHAR('=') WS NT("l_expr3") TREE("ne")

	NT_DEF("l_expr5")
		RULE NTP("l_expr4")
		REC_RULEC WS CHAR('^') WS NT("l_expr4") TREE("bexor")

	NT_DEF("l_expr6")
		RULE NTP("l_expr5")
		REC_RULEC WS CHAR('&') WS NT("l_expr5") TREE("land")

	NT_DEF("l_expr7")
		RULE NTP("l_expr6")
		REC_RULEC WS CHAR('|') WS NT("l_expr6") TREE("lor")

	NT_DEF("l_expr8")
		RULE NTP("l_expr7")
		REC_RULEC WS CHAR('&') CHAR('&') WS NT("l_expr7") TREE("and")

	NT_DEF("l_expr9")
		RULE NTP("l_expr8")
		REC_RULEC WS CHAR('|') CHAR('|') WS NT("l_expr8") TREE("or")

	NT_DEF("conditional_expr")
		RULE NT("l_expr9") WS CHAR('?') WS NT("l_expr9") WS CHAR(':') WS NT("conditional_expr") TREE("if_expr")
		RULE NTP("l_expr9")

	NT_DEF("assignment_expr")
		RULE NT("unary_expr") WS NT("assignment_operator") WS NT("assignment_expr") TREE("assignment")
		RULE NTP("conditional_expr")

	NT_DEF("assignment_operator")
		RULE CHAR('=') TREE("ass")
		RULE CHAR('*') CHAR('=') WS TREE("times_ass")
		RULE CHAR('/') CHAR('=') WS TREE("div_ass")
		RULE CHAR('%') CHAR('=') WS TREE("mod_ass")
		RULE CHAR('+') CHAR('=') WS TREE("add_ass")
		RULE CHAR('-') CHAR('=') WS TREE("sub_ass")
		RULE CHAR('<') CHAR('<') CHAR('=') WS TREE("sl_ass")
		RULE CHAR('>') CHAR('>') CHAR('=') WS TREE("sr_ass")
		RULE CHAR('&') CHAR('=') WS TREE("and_ass")
		RULE CHAR('|') CHAR('=') WS TREE("or_ass")
		RULE CHAR('^') CHAR('=') WS TREE("exor_ass")

	NT_DEF("expr")
		RULE NT("assignment_expr") SEQL { CHAIN CHAR(',') WS } PASS

	NT_DEF("constant_expr")
		RULE NT("conditional_expr") PASS

	NT_DEF("declaration")
		RULE
		{ GROUPING
			RULE NT("storage_class_specifier")
			RULE NT("type_specifier")
		} SEQL OPTN AVOID
		{ GROUPING
			RULE
			{ GROUPING
				RULE NT("declarator")
				{ GROUPING
					RULE WS CHAR('=') WS NT("initializer")
				} OPTN
			} SEQL { CHAIN CHAR(',') WS } OPTN CHAR(';') TREE("decl")
			RULE NT("func_declarator") CHAR('(') NT("parameter_declaration_list") OPTN CHAR(')')
			{ GROUPING
				RULE CHAR(';')
				RULE CHAR('{') NT("decl_or_stat") CHAR('}')
			} TREE("new_style")
			RULE NT("func_declarator") CHAR('(') NT("ident_list") OPTN CHAR(')') NT("declaration") SEQL OPTN CHAR('{') NT("decl_or_stat") CHAR('}') TREE("old_style")
		}

	NT_DEF("storage_class_specifier")
		RULE KEYWORD("typedef") TREE("typedef")
		RULE KEYWORD("extern") TREE("extern")
		RULE KEYWORD("static") TREE("static")
		RULE KEYWORD("auto") TREE("auto")
		RULE KEYWORD("register") TREE("register")

	NT_DEF("type_specifier")
		RULE KEYWORD("char") TREE("char")
		RULE KEYWORD("short") TREE("short")
		RULE KEYWORD("int") TREE("int")
		RULE KEYWORD("long") TREE("long")
		RULE KEYWORD("signed") TREE("signed")
		RULE KEYWORD("unsigned") TREE("unsigned")
		RULE KEYWORD("float") TREE("float")
		RULE KEYWORD("double") TREE("double")
		RULE KEYWORD("const") TREE("const")
		RULE KEYWORD("volatile") TREE("volatile")
		RULE KEYWORD("void") TREE("void")
		RULE NT("struct_or_union_specifier")
		RULE NT("enum_specifier")
		RULE IDENT

	NT_DEF("struct_or_union_specifier")
		RULE KEYWORD("struct") IDENT CHAR('{')
		{ GROUPING
			RULE NT("struct_declaration")
		} SEQL CHAR('}') TREE("struct_d")
		RULE KEYWORD("struct") CHAR('{')
		{ GROUPING
			RULE NT("struct_declaration")
		} SEQL CHAR('}') TREE("struct_n")
		RULE KEYWORD("struct") IDENT TREE("struct")
		RULE KEYWORD("union") IDENT CHAR('{')
		{ GROUPING
			RULE NT("struct_declaration")
		} SEQL CHAR('}') TREE("union_d")
		RULE KEYWORD("union") CHAR('{')
		{ GROUPING
			RULE NT("struct_declaration")
		} SEQL CHAR('}') TREE("union_n")
		RULE KEYWORD("union") IDENT TREE("union")

	NT_DEF("struct_declaration")
		RULE NT("type_specifier") NT("struct_declaration") TREE("type")
		RULE NT("struct_declarator") SEQL { CHAIN CHAR(',') WS } CHAR(';') TREE("strdec")

	NT_DEF("struct_declarator")
		RULE NT("declarator")
		{ GROUPING
			RULE CHAR(':') NT("constant_expr")
		} OPTN TREE("record_field")

	NT_DEF("enum_specifier")
		RULE KEYWORD("enum") IDENT
		{ GROUPING
			RULE CHAR('{') NT("enumerator") SEQL { CHAIN CHAR(',') WS } CHAR('}')
		} TREE("enum")

	NT_DEF("enumerator")
		RULE IDENT
		{ GROUPING
			RULE CHAR('=') NT("constant_expr")
		} OPTN TREE("enumerator")

	NT_DEF("func_declarator")
		RULE CHAR('*')
		{ GROUPING
			RULE KEYWORD("const") TREE("const")
		} OPTN NT("func_declarator") TREE("pointdecl")
		RULE KEYWORD("(") NT("func_declarator") CHAR(')')
		RULE IDENT

	NT_DEF("declarator")
		RULE CHAR('*')
		{ GROUPING
			RULE KEYWORD("const") TREE("const")
		} OPTN NT("declarator") TREE("pointdecl")
		RULE CHAR('(') NT("declarator") CHAR(')') TREE("brackets")
		RULE WS IDENT
		REC_RULEC CHAR('[') NT("constant_expr") OPTN CHAR(']') TREE("array")
		REC_RULEC CHAR('(') NT("abstract_declaration_list") OPTN CHAR(')') TREE("function")

	NT_DEF("abstract_declaration_list")
		RULE NT("abstract_declaration")
		{ GROUPING
			RULE CHAR(',')
			{ GROUPING
				RULE CHAR('.') CHAR('.') CHAR('.') TREE("varargs")
				RULE NT("abstract_declaration_list")
			}
		} OPTN

	NT_DEF("parameter_declaration_list")
		RULE NT("parameter_declaration")
		{ GROUPING
			RULE CHAR(',')
			{ GROUPING
				RULE CHAR('.') CHAR('.') CHAR('.') TREE("varargs")
				RULE NT("parameter_declaration_list")
			}
		} OPTN

	NT_DEF("ident_list")
		RULE IDENT
		{ GROUPING
			RULE CHAR(',')
			{ GROUPING
				RULE CHAR('.') CHAR('.') CHAR('.') TREE("varargs")
				RULE NT("ident_list")
			}
		} OPTN

	NT_DEF("parameter_declaration")
		RULE NT("type_specifier") NT("parameter_declaration") TREE("type")
		RULE NT("declarator")
		RULE NT("abstract_declarator")

	NT_DEF("abstract_declaration")
		RULE NT("type_specifier") NT("parameter_declaration") TREE("type")
		RULE NT("abstract_declarator")

	NT_DEF("abstract_declarator")
		RULE CHAR('*')
		{ GROUPING
			RULE KEYWORD("const") TREE("const")
		} OPTN NT("abstract_declarator") TREE("abs_pointdecl")
		RULE CHAR('(') NT("abstract_declarator") CHAR(')') TREE("abs_brackets")
		RULE
		REC_RULEC CHAR('[') NT("constant_expr") OPTN CHAR(']') TREE("abs_array")
		REC_RULEC CHAR('(') NT("parameter_declaration_list") CHAR(')') TREE("abs_func")

	NT_DEF("initializer")
		RULE NT("assignment_expr")
		RULE CHAR('{') NT("initializer") SEQL { CHAIN CHAR(',') WS } CHAR(',') OPTN CHAR('}') TREE("initializer")

	NT_DEF("decl_or_stat")
		RULE NT("declaration") SEQL OPTN NT("statement") SEQL OPTN

	NT_DEF("statement")
		RULE
		{ GROUPING
			RULE
			{ GROUPING
				RULE IDENT
				RULE KEYWORD("case") NT("constant_expr")
				RULE KEYWORD("default")
			} CHAR(':') NT("statement") TREE("label")
			RULE CHAR('{') NT("decl_or_stat") CHAR('}') TREE("brackets")
		}
		RULE
		{ GROUPING
			RULE NT("expr") OPTN CHAR(';')
			RULE KEYWORD("if") WS CHAR('(') NT("expr") CHAR(')') NT("statement")
			{ GROUPING
				RULE KEYWORD("else") NT("statement")
			} OPTN TREE("if")
			RULE KEYWORD("switch") WS CHAR('(') NT("expr") CHAR(')') NT("statement") TREE("switch")
			RULE KEYWORD("while") WS CHAR('(') NT("expr") CHAR(')') NT("statement") TREE("while")
			RULE KEYWORD("do") NT("statement") KEYWORD("while") WS CHAR('(') NT("expr") CHAR(')') CHAR(';') TREE("do")
			RULE KEYWORD("for") WS CHAR('(') NT("expr") OPTN CHAR(';')
			{ GROUPING
				RULE WS NT("expr")
			} OPTN CHAR(';')
			{ GROUPING
				RULE WS NT("expr")
			} OPTN CHAR(')') NT("statement") TREE("for")
			RULE KEYWORD("goto") IDENT CHAR(';') TREE("goto")
			RULE KEYWORD("continue") CHAR(';') TREE("cont")
			RULE KEYWORD("break") CHAR(';') TREE("break")
			RULE KEYWORD("return") NT("expr") OPTN CHAR(';') TREE("ret")
		}

	NT_DEF("root")
		RULE
		{ GROUPING
			RULE NT("declaration")
		} SEQL OPTN END
}


/*
	Fixed string output stream
	~~~~~~~~~~~~~~~~~~~~~~~~~~
*/

typedef struct fixed_string_ostream fixed_string_ostream_t, *fixed_string_ostream_p;
struct fixed_string_ostream
{
	ostream_t ostream;
	char *buffer;
	unsigned int i;
	unsigned int len;
};

void fixed_string_ostream_put(ostream_p ostream, char ch)
{
	if (((fixed_string_ostream_p)ostream)->i < ((fixed_string_ostream_p)ostream)->len)
		((fixed_string_ostream_p)ostream)->buffer[((fixed_string_ostream_p)ostream)->i++] = ch;
}

void fixed_string_ostream_init(fixed_string_ostream_p ostream, char *buffer, unsigned int len)
{
	ostream->ostream.put = fixed_string_ostream_put;
	ostream->buffer = buffer;
	ostream->i = 0;
	ostream->len = len - 1;
}

void fixed_string_ostream_finish(fixed_string_ostream_p ostream)
{
	ostream->buffer[ostream->i] = '\0';
}




void test_parse_grammar(non_terminal_dict_p *all_nt, const char *nt, const char *input, const char *exp_output)
{
	text_buffer_t text_buffer;
	text_buffer_assign_string(&text_buffer, input);
	
	solutions_t solutions;
	solutions_init(&solutions, &text_buffer);
	
	parser_t parser;
	parser_init(&parser, &text_buffer);
	parser.cache_hit_function = solutions_find;
	parser.cache = &solutions;
	
	DECL_RESULT(result);
	if (parse_nt(&parser, find_nt(nt, all_nt), &result) && text_buffer_end(&text_buffer))
	{
		if (result.data == NULL)
			fprintf(stderr, "ERROR: parsing '%s' did not return result\n", input);
		else
		{
			char output[200];
			fixed_string_ostream_t fixed_string_ostream;
			fixed_string_ostream_init(&fixed_string_ostream, output, 200);
			result_print(&result, &fixed_string_ostream.ostream);
			fixed_string_ostream_finish(&fixed_string_ostream);
			if (strcmp(output, exp_output) != 0)
			{
				fprintf(stderr, "ERROR: parsed value '%s' from '%s' instead of expected '%s'\n",
						output, input, exp_output);
			}
			else
				fprintf(stderr, "OK: parsed '%s' to '%s'\n", input, output);
		}
	}
	else
	{
		fprintf(stderr, "ERROR: failed to parse ident from '%s'\n", input);
	}
	DISP_RESULT(result);
	
	solutions_free(&solutions);
}

void test_c_grammar(non_terminal_dict_p *all_nt)
{
	test_parse_grammar(all_nt, "expr", "a", "list(a)");
	test_parse_grammar(all_nt, "expr", "a*b", "list(times(a,b))");
}

/*
	File output stream
	~~~~~~~~~~~~~~~~~~
*/

typedef struct file_ostream file_ostream_t, *file_ostream_p;
struct file_ostream
{
	ostream_t ostream;
	FILE *f;
};

void file_ostream_put(ostream_p ostream, char ch)
{
	if (((file_ostream_p)ostream)->f != NULL)
		fputc(ch, ((file_ostream_p)ostream)->f);
}

void file_ostream_init(file_ostream_p ostream, FILE *f)
{
	ostream->ostream.put = file_ostream_put;
	ostream->f = f;
}


int main(int argc, char *argv[])
{
	file_ostream_t debug_ostream;
	file_ostream_init(&debug_ostream, stdout);
	stdout_stream = &debug_ostream.ostream;
	
	non_terminal_dict_p all_nt = NULL;

	white_space_grammar(&all_nt);
	test_white_space_grammar(&all_nt);

	number_grammar(&all_nt);
	test_number_grammar(&all_nt);

	ident_grammar(&all_nt);
	test_ident_grammar(&all_nt);
	
	char_grammar(&all_nt);
	test_char_grammar(&all_nt);

	non_terminal_dict_p all_nt_c_grammar = NULL;
	c_grammar(&all_nt_c_grammar);
    test_c_grammar(&all_nt_c_grammar);

	return 0;
}





#if 0


/*
	Error reporting
	~~~~~~~~~~~~~~~
	In case the input cannot be parsed, the parser produces
	a list of all expected terminal symbols together with
	the grammar rule they occured in, at the position the 
	parser could not continue.
	The following section implements this list.
*/

#define MAX_EXP_SYM 200
typedef struct {
  char *in_nt;
  element_p elements;
} expect_t;
expect_t expect[MAX_EXP_SYM];
char *exp_in_nt[MAX_EXP_SYM];

static void expected_string(char *s, char *is_keyword)
{
	if (file_pos < f_file_pos)
		return;
	if (file_pos > f_file_pos)
	{   nr_exp_syms = 0;
		f_file_pos = file_pos;
		f_line = cur_line;
		f_column = cur_column;
	}

	if (nr_exp_syms >= MAX_EXP_SYM)
		return;

	expect[nr_exp_syms].sym = s;
	expect[nr_exp_syms].in_nt = current_nt;
	expect[nr_exp_syms].elements = current_rule;
	expect[nr_exp_syms].is_keyword = is_keyword;
	nr_exp_syms++;
}

void print_last_pos()
{   int i;

	printf("%d.%d Expected:\n", f_line, f_column);
	for (i = 0; i < nr_exp_syms; i++)
	{   bool unique = TRUE;
		int j;
		for (j = 0; unique && j < i; j++)
			if (expect[i].elements == expect[j].elements)
				unique = FALSE;
		if (unique)
		{
			printf("	");
			if (expect[i].elements)
				print_rule(stdout, expect[i].elements);
			else
				printf("%s ", expect[i].sym);
			printf(":");
			if (expect[i].in_nt)
				printf(" in %s", expect[i].in_nt);
			if (expect[i].is_keyword != NULL)
				printf(" ('%s' is a keyword)", expect[i].is_keyword);
			printf("\n");
		}
	}
}





#endif
