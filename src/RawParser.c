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
#include <stdlib.h>
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
	fprintf(stdout, "At line %u: allocated %lu bytes %p\n", line, size, p);
	return p;
}

void my_free(void *p, unsigned int line)
{

	fprintf(stdout, "At line %u: freed %p\n", line, p);
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
	bool back_tracking;         /* Whether a sequence is back-tracking */
	bool avoid;                 /* Whether the elmeent should be avoided when it is optional and/or sequential. */
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
	element->back_tracking = FALSE;
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
	{
		if (element->chain_rule == NULL)
			fprintf(f, "SEQ ");
		else
		{
			fprintf(f, "CHAIN (");
			element_print(f, element->chain_rule);
			fprintf(f, ")");
		}
		if (element->back_tracking)
			fprintf(f, "BACK_TRACKING ");
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
#define BACK_TRACKING element->back_tracking = TRUE;
#define AVOID element->avoid = TRUE;
#define SET_PS(F) element->set_pos = F;
#define CHAR(C) _NEW_GR(rk_char) element->info.ch = C;
#define CHARF(C,F) CHAR(C) element->add_char_function = F;
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
#ifdef CHECK_LOCAL_RESULT
	int line;
	const char *name;
	result_p context;
#endif
};

/*
	- Function to initialize a result
*/

#ifdef CHECK_LOCAL_RESULT
#define CHECK_LOCAL_PARAM(P) , P
#define RESULT_INIT(V) result_init(V, 0, __LINE__, "*")
#define RESULT_RELEASE(V) result_release(V, 0, __LINE__)
#else
#define CHECK_LOCAL_PARAM(P)
#define RESULT_INIT(V) result_init(V)
#define RESULT_RELEASE(V) result_release(V)
#endif

void result_init(result_p result CHECK_LOCAL_PARAM(result_p *context) CHECK_LOCAL_PARAM(int line) CHECK_LOCAL_PARAM(const char *name))
{
	result->data = NULL;
	result->inc = 0;
	result->dec = 0;
	result->print = 0;
#ifdef CHECK_LOCAL_RESULT
	result->line = line;
	result->name = name;
	if (context != NULL)
	{
		result->context = *context;
		*context = result;
	}
	else
		result->context = 0;
#endif
}

/*
	- Function to assign result to another result
*/

void result_assign(result_p trg, result_p src)
{
	void (*old_trg_dec)(void *data) = trg->dec;
	void *old_trg_data = trg->data;
	if (src->inc != 0 && src->data != 0)
		src->inc(src->data);
	trg->data = src->data;
	trg->inc = src->inc;
	trg->dec = src->dec;
	trg->print = src->print;
	if (old_trg_dec != 0)
		old_trg_dec(old_trg_data);
}

/*
	- Function to transfer the result to another result.
	  (The source will be initialized.)
*/

void result_transfer(result_p trg, result_p src)
{
	void (*old_trg_dec)(void *data) = trg->dec;
	void *old_trg_data = trg->data;
	trg->data = src->data;
	trg->inc = src->inc;
	trg->dec = src->dec;
	trg->print = src->print;
	RESULT_INIT(src);
	if (old_trg_dec != 0)
		old_trg_dec(old_trg_data);
}

/*
	- Function to release the result
*/

void result_release(result_p result CHECK_LOCAL_PARAM(result_p *context) CHECK_LOCAL_PARAM(int line))
{
#ifdef CHECK_LOCAL_RESULT
	if (context != NULL)
	{
		if (*context == NULL)
		{
			printf("Context already empty on line %d. Found from line %d.\n", line, result->line);
			exit(1);
		}
		if (*context != result)
		{
			printf("Wrong context on line %d. Found from line %d. Expect from line %s  (on %d)\n",
				line, result->line, (*context)->name, (*context)->line);
			exit(1);
		}
		*context = result->context;
	}
#endif
	if (result->dec != 0 && result->data != 0)
		result->dec(result->data);
	result->data = NULL;
	result->inc = 0;
	result->dec = 0;
	result->data = NULL;
	result->print = 0;
}

/*
	- Function to print the result
*/

void result_print(result_p result, ostream_p ostream)
{
	if (result->print == 0 || result->data == NULL)
		ostream_puts(ostream, "<>");
	else
		result->print(result->data, ostream);
}


/*
	- Two macro definitions which should be used a the start and end of
	  the scope of a result variable
*/

#ifdef CHECK_LOCAL_RESULT
#define ENTER_RESULT_CONTEXT result_p result_p_context = 0;
#define EXIT_RESULT_CONTEXT if (result_p_context != 0) { printf("On line %d context not closed for %s (on %d)\n", __LINE__, result_p_context->name, result_p_context->line); exit(1); }
#define DECL_RESULT(V) result_t V; result_init(&V, &result_p_context, __LINE__, #V);
#define DISP_RESULT(V) result_release(&V, &result_p_context, __LINE__);
#else
#define ENTER_RESULT_CONTEXT
#define INIT_RESULT(V) result_init(&V)
#define EXIT_RESULT_CONTEXT
#define DECL_RESULT(V) result_t V; result_init(&V);
#define DISP_RESULT(V) result_release(&V);
#endif

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
#ifdef SAFE_CASTING
	const char* type_name;
#endif
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

#ifdef SAFE_CASTING
#define SET_TYPE(T, X) ((ref_counted_base_p)X)->type_name = T;
#define CAST(T,X) ((T)check_type(#T,X,__LINE__))

void *check_type(const char *type_name, void *value, int line)
{
	if (value == 0) return NULL;
	const char *value_type_name = ((ref_counted_base_p)value)->type_name;
	if (strcmp(value_type_name, type_name) != 0)
	{
		printf("line %d Error: castring %s to %s\n", line, ((ref_counted_base_p)value)->type_name, type_name); fflush(stdout);
		exit(1);
		return NULL;
	}
	return value;
}
#else
#define SET_TYPE(T, X)
#define CAST(T,X) ((T)(X))
#endif

void result_assign_ref_counted(result_p result, void *data, void (*print)(void *data, ostream_p ostream))
{
	if (debug_allocations) fprintf(stdout, "Allocated %p\n", data);
	((ref_counted_base_p)data)->cnt = 1;
#ifdef SAFE_CASTING
	((ref_counted_base_p)data)->type_name = "";
#endif
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

#define NUMBER_DATA_NUM(R) (CAST(number_data_p,(R)->data)->num)

void number_print(void *data, ostream_p ostream)
{
	char buffer[41];
	snprintf(buffer, 40, "number %ld", CAST(number_data_p, data)->num);
	ostream_puts(ostream, buffer);
}

void new_number_data(result_p result)
{
	number_data_p number_data = MALLOC(struct number_data);
	number_data->_base.release = 0;
	result_assign_ref_counted(result, number_data, number_print);
	SET_TYPE("number_data_p",number_data);
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
#define DEBUG_ENTER_P2(X,A,B) if (debug_parse) { DEBUG_TAB; printf("Enter: "); printf(X,A,B); depth += 2; }
#define DEBUG_ENTER_P3(X,A,B,C) if (debug_parse) { DEBUG_TAB; printf("Enter: "); printf(X,A,B,C); depth += 2; }
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

typedef struct nt_stack *nt_stack_p;

typedef struct
{
	text_buffer_p text_buffer;
	nt_stack_p nt_stack;
	cache_item_p (*cache_hit_function)(void *cache, size_t pos, const char *nt);
	void *cache;
} parser_t, *parser_p;

void parser_init(parser_p parser, text_buffer_p text_buffer)
{
	parser->text_buffer = text_buffer;
	parser->nt_stack = NULL;
	parser->cache_hit_function = 0;
	parser->cache = NULL;
}

nt_stack_p nt_stack_push(const char *name, parser_p parser);
nt_stack_p nt_stack_pop(nt_stack_p cur);

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
	ENTER_RESULT_CONTEXT
	const char *nt = non_term->name;

	DEBUG_ENTER_P3("parse_nt(%s) at %d.%d", nt, parser->text_buffer->pos.cur_line, parser->text_buffer->pos.cur_column); DEBUG_NL;

	/* First try the cache (if available) */
	cache_item_p cache_item = NULL;
	if (parser->cache_hit_function != NULL)
	{
		cache_item = parser->cache_hit_function(parser->cache, parser->text_buffer->pos.pos, nt);
		if (cache_item != NULL)
		{
			if (cache_item->success == s_success)
			{
				DEBUG_EXIT_P1("parse_nt(%s) CACHE SUCCESS = ", nt);  DEBUG_PT(&cache_item->result)  DEBUG_NL;
				result_assign(result, &cache_item->result);
				text_buffer_set_pos(parser->text_buffer, &cache_item->next_pos);
				EXIT_RESULT_CONTEXT
				return TRUE;
			}
			else if (cache_item->success == s_fail)
			{
				DEBUG_EXIT_P1("parse_nt(%s) CACHE FAIL", nt);  DEBUG_NL;
				EXIT_RESULT_CONTEXT
				return FALSE;
			}
			cache_item->success = s_fail; // To deal with indirect left-recurssion
		}
	}
	
	/* Push the current non-terminal on stack */
	parser->nt_stack = nt_stack_push(nt, parser);

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
			DISP_RESULT(start)
			break;
		}
		DISP_RESULT(start)
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
		
		/* Pop the current non-terminal from the stack */
		parser->nt_stack = nt_stack_pop(parser->nt_stack);
		
		EXIT_RESULT_CONTEXT
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
	
	/* Update the cache item, if available */
	if (cache_item != NULL)
	{
		result_assign(&cache_item->result, result);
		cache_item->success = s_success;
		cache_item->next_pos = parser->text_buffer->pos;
	}

	/* Pop the current non-terminal from the stack */
	parser->nt_stack = nt_stack_pop(parser->nt_stack);
	
	EXIT_RESULT_CONTEXT
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
	ENTER_RESULT_CONTEXT
	DEBUG_ENTER_P2("parse_rule at %d.%d: ", parser->text_buffer->pos.cur_line, parser->text_buffer->pos.cur_column);
	DEBUG_PR(element); DEBUG_NL;

	if (element == NULL)
	{
		/* At the end of the rule: */
		if (rule == NULL || rule->end_function == 0)
			result_assign(rule_result, prev_result);
		else if (!rule->end_function(prev_result, rule->end_function_data, rule_result))
		{
			DEBUG_EXIT("parse_rule failed by end function "); DEBUG_NL;
			return FALSE;
		}
		DEBUG_EXIT("parse_rule = ");
		DEBUG_PT(rule_result); DEBUG_NL;
		EXIT_RESULT_CONTEXT
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
				EXIT_RESULT_CONTEXT
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
				EXIT_RESULT_CONTEXT
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
			EXIT_RESULT_CONTEXT
			return TRUE;
		}
		DISP_RESULT(skip_result);
	}
		
	/* Store the current position */
	text_pos_t sp = parser->text_buffer->pos;
	
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
			if (element->back_tracking)
			{
				/* Now parse the remainder elements of the sequence (and thereafter the remainder of the rule. */
				if (parse_seq(parser, element, &seq_elem, prev_result, rule, rule_result))
				{
					DISP_RESULT(seq_elem);
					DISP_RESULT(seq_begin);
					DEBUG_EXIT("parse_rule = ");
					DEBUG_PT(rule_result); DEBUG_NL;
					EXIT_RESULT_CONTEXT
					return TRUE;
				}
			}
			else
			{
				/* Now continue parsing more elements */
				for (;;)
				{
					if (element->avoid)
					{
						DECL_RESULT(result);
						if (element->add_seq_function != NULL && !element->add_seq_function(prev_result, &seq_elem, &result))
						{
							DEBUG_TAB; DEBUG_("add_seq_function failed\n");
							break;
						}
						if (parse_rule(parser, element->next, &result, rule, rule_result))
						{
							DISP_RESULT(result);
							DISP_RESULT(seq_elem);
							DISP_RESULT(seq_begin);
							DEBUG_EXIT("parse_rule = ");
							DEBUG_PT(rule_result); DEBUG_NL;
							EXIT_RESULT_CONTEXT
							return TRUE;
						}
						DISP_RESULT(result);
					}
					
					/* Store the current position */
					text_pos_t sp = parser->text_buffer->pos;
					
					if (element->chain_rule != NULL)
					{
						DECL_RESULT(dummy_prev_result);
						DECL_RESULT(dummy_chain_elem);
						bool parsed_chain = parse_rule(parser, element->chain_rule, &dummy_prev_result, NULL, &dummy_chain_elem);
						DISP_RESULT(dummy_chain_elem);
						DISP_RESULT(dummy_prev_result);
						if (!parsed_chain)
							break;
					}
					
					DECL_RESULT(next_seq_elem);
					if (parse_element(parser, element, &seq_elem, &next_seq_elem))
					{
						result_assign(&seq_elem, &next_seq_elem);
					}
					else
					{
						/* Failed to parse the next element of the sequence: reset the current position to the saved position. */
						text_buffer_set_pos(parser->text_buffer, &sp);
						DISP_RESULT(next_seq_elem);
						break;
					}
					DISP_RESULT(next_seq_elem);
				}
				
				DECL_RESULT(result);
				if (element->add_seq_function != NULL && !element->add_seq_function(prev_result, &seq_elem, &result))
				{
					DEBUG_TAB; DEBUG_("add_seq_function failed\n");
				}
				else
				{
					if (parse_rule(parser, element->next, &result, rule, rule_result))
					{
						DISP_RESULT(result);
						DISP_RESULT(seq_elem);
						DISP_RESULT(seq_begin);
						DEBUG_EXIT("parse_rule = ");
						DEBUG_PT(rule_result); DEBUG_NL;
						EXIT_RESULT_CONTEXT
						return TRUE;
					}
				}
				DISP_RESULT(result);
			}
		}
		DISP_RESULT(seq_elem);
		DISP_RESULT(seq_begin);
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
				EXIT_RESULT_CONTEXT
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
				EXIT_RESULT_CONTEXT
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
				EXIT_RESULT_CONTEXT
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
			EXIT_RESULT_CONTEXT
			return TRUE;
		}
		DISP_RESULT(skip_result);
	}

    DEBUG_EXIT("parse_rule: failed"); DEBUG_NL;
	EXIT_RESULT_CONTEXT
	return FALSE;
}

bool parse_seq(parser_p parser, element_p element, const result_p prev_seq, const result_p prev, rules_p rule, result_p rule_result)
{
	ENTER_RESULT_CONTEXT
	/* In case of the avoid modifier, first an attempt is made to parse the
	   remained of the rule */
	if (element->avoid)
	{
		DECL_RESULT(result);
		if (element->add_seq_function != NULL && !element->add_seq_function(prev, prev_seq, &result))
		{
			DISP_RESULT(result);
			EXIT_RESULT_CONTEXT
			return FALSE;
		}
		if (parse_rule(parser, element->next, &result, rule, rule_result))
		{
			DISP_RESULT(result);
			EXIT_RESULT_CONTEXT
			return TRUE;
		}
		DISP_RESULT(result);
	}
	
	/* Store the current position */
	text_pos_t sp = parser->text_buffer->pos;

	/* If a chain rule is defined, try to parse it.*/
	bool go = TRUE;
	if (element->chain_rule != NULL)
	{
		DECL_RESULT(dummy_prev_result);
		DECL_RESULT(dummy_chain_elem);
		go = parse_rule(parser, element->chain_rule, &dummy_prev_result, NULL, &dummy_chain_elem);
		DISP_RESULT(dummy_prev_result);
		DISP_RESULT(dummy_chain_elem);
	}
	if (go)
	{
		/* Try to parse the next element of the sequence */
		DECL_RESULT(seq_elem);
		if (parse_element(parser, element, prev_seq, &seq_elem))
		{
			/* If succesful, try to parse the remainder of the sequence (and thereafter the remainder of the rule) */
			if (parse_seq(parser, element, &seq_elem, prev, rule, rule_result))
			{
				DISP_RESULT(seq_elem);
				EXIT_RESULT_CONTEXT
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
			EXIT_RESULT_CONTEXT
			return FALSE;
		}
		
		if (parse_rule(parser, element->next, &result, rule, rule_result))
		{
			DISP_RESULT(result);
			EXIT_RESULT_CONTEXT
			return TRUE;
		}
		DISP_RESULT(result);
	}
	
	EXIT_RESULT_CONTEXT
	return FALSE;
}


/*
	Parse an element
	~~~~~~~~~~~~~~~~
	
	The following function is used to parse a part of an element, not dealing
	with if the element is optional or a sequence.
*/

void expect_element(parser_p parser, element_p element);

bool parse_element(parser_p parser, element_p element, const result_p prev_result, result_p result)
{
	ENTER_RESULT_CONTEXT
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
					EXIT_RESULT_CONTEXT
					return FALSE;
				}
				
				/* If there is a condition, evaluate the result */
				if (element->condition != 0 && !(*element->condition)(&nt_result, element->condition_argument))
				{
					DISP_RESULT(nt_result)
					text_buffer_set_pos(parser->text_buffer, &sp);
					EXIT_RESULT_CONTEXT
					return FALSE;
				}
				
				/* Combine the result with the previous result */
				if (element->add_function == 0)
					result_assign(result, prev_result);
				else if (!(*element->add_function)(prev_result, &nt_result, result))
				{
					DISP_RESULT(nt_result)
					text_buffer_set_pos(parser->text_buffer, &sp);
					EXIT_RESULT_CONTEXT
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
					EXIT_RESULT_CONTEXT
					return FALSE;
				}
				
				/* Combine the result of the rule with the previous result */
				if (element->add_function == 0)
					result_assign(result, &rule_result);
				else if (!(*element->add_function)(prev_result, &rule_result, result))
				{
					DISP_RESULT(rule_result)
					text_buffer_set_pos(parser->text_buffer, &sp);
					EXIT_RESULT_CONTEXT
					return FALSE;
				}
				DISP_RESULT(rule_result)
			}
			break;
		case rk_end:
			/* Check if the end of the buffer is reached */
			if (!text_buffer_end(parser->text_buffer))
			{
				expect_element(parser, element);
				EXIT_RESULT_CONTEXT
				return FALSE;
			}
			result_assign(result, prev_result);
			break;
		case rk_char:
			/* Check if the specified character is found at the current position in the text buffer */
			if (*parser->text_buffer->info != element->info.ch)
			{
				expect_element(parser, element);
				EXIT_RESULT_CONTEXT
				return FALSE;
			}
			/* Advance the current position of the text buffer */
			text_buffer_next(parser->text_buffer);
			/* Process the character */
			if (element->add_char_function == 0)
				result_assign(result, prev_result);
			else if (!(*element->add_char_function)(prev_result, element->info.ch, result))
			{
				EXIT_RESULT_CONTEXT
				return FALSE;
			}
			break;
		case rk_charset:
			/* Check if the character at the current position in the text buffer is found in the character set */
			if (!char_set_contains(element->info.char_set, *parser->text_buffer->info))
			{
				expect_element(parser, element);
				EXIT_RESULT_CONTEXT
				return FALSE;
			}
			{
				/* Remember the character and advance the current position of the text buffer */
				char ch = *parser->text_buffer->info;
				text_buffer_next(parser->text_buffer);
				/* Process the character */
				if (element->add_char_function == 0)
					result_assign(result, prev_result);
				else if (!(*element->add_char_function)(prev_result, ch, result))
				{
					EXIT_RESULT_CONTEXT
					return FALSE;
				}
			}
			break;
		case rk_term:
			/* Call the terminal parse function and see if it has parsed something */
			{
				const char *next_pos = element->info.terminal_function(parser->text_buffer->info, result);
				/* If the start position is returned, assume that it failed. */
				if (next_pos <= parser->text_buffer->info)
				{
					expect_element(parser, element);
					EXIT_RESULT_CONTEXT
					return FALSE;
				}
				/* Increment the buffer till the returned position */
				while (parser->text_buffer->info < next_pos)
					text_buffer_next(parser->text_buffer);
			}
			break;
		default:
			EXIT_RESULT_CONTEXT
			return FALSE;
			break;
	}
	
	/* Set the position on the result */
	if (element->set_pos != NULL)
		element->set_pos(result, &sp);

	EXIT_RESULT_CONTEXT
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
	RESULT_INIT(&sol->cache_item.result);
	solutions->sols[pos] = sol;
	return &sol->cache_item;
}

/*
	White space tests
	~~~~~~~~~~~~~~~~~
*/

void test_parse_white_space(non_terminal_dict_p *all_nt, const char *input)
{
	ENTER_RESULT_CONTEXT

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

	EXIT_RESULT_CONTEXT
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
	ENTER_RESULT_CONTEXT
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
		else if (CAST(number_data_p, result.data)->num != num)
			fprintf(stderr, "ERROR: parsed value %ld from '%s' instead of expected %d\n",
				CAST(number_data_p, result.data)->num, input, num);
		else
			fprintf(stderr, "OK: parsed value %ld from '%s'\n", CAST(number_data_p, result.data)->num, input);
	}
	else
		fprintf(stderr, "ERROR: failed to parse number from '%s'\n", input);
	DISP_RESULT(result);

	solutions_free(&solutions);
	EXIT_RESULT_CONTEXT
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
	tree_p tree = CAST(tree_p, data);

	alloced_trees--;

	if (tree->nr_children > 0)
	{
		for (int i = 0; i < tree->nr_children; i++)
			RESULT_RELEASE(&tree->children[i]);
		FREE(tree->children);
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
	prev_child_p prev_child = CAST(prev_child_p, data);
	RESULT_RELEASE(&prev_child->child);
	if (prev_child != NULL)
		ref_counted_base_dec(prev_child);
	FREE(prev_child);
}

prev_child_p malloc_prev_child()
{
	prev_child_p new_prev_child = MALLOC(struct prev_child_t);
	new_prev_child->_base.cnt = 1;
	new_prev_child->_base.release = release_prev_child;
	RESULT_INIT(&new_prev_child->child);
	new_prev_child->prev = NULL;
	return new_prev_child;
}

bool add_child(result_p prev, result_p elem, result_p result)
{
	prev_child_p prev_child = CAST(prev_child_p, prev->data);
	if (prev_child != NULL)
		ref_counted_base_inc(prev_child);
	prev_child_p new_prev_child = malloc_prev_child();
	new_prev_child->prev = prev_child;
	result_assign(&new_prev_child->child, elem);
	result_assign_ref_counted(result, new_prev_child, NULL);
	SET_TYPE("prev_child_p", new_prev_child);
	return TRUE;
}

bool rec_add_child(result_p rec_result, result_p result)
{
	prev_child_p new_prev_child = malloc_prev_child();
	new_prev_child->prev = NULL;
	result_assign(&new_prev_child->child, rec_result);
	result_assign_ref_counted(result, new_prev_child, NULL);
	SET_TYPE("prev_child_p", new_prev_child);
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
		RESULT_INIT(&tree->children[i]);
		result_assign(&tree->children[i], &child->child);
	}
	return tree;
}

void tree_print(void *data, ostream_p ostream)
{
	tree_p tree = CAST(tree_p, data);
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
	prev_child_p children = CAST(prev_child_p, rule_result->data);
	const char *name = (const char*)data;
	tree_p tree = make_tree_with_children(name, children);
	result_assign_ref_counted(result, tree, tree_print);
	SET_TYPE("tree_p", tree);
	return TRUE;
}

bool pass_tree(const result_p rule_result, void* data, result_p result)
{
	prev_child_p child = CAST(prev_child_p, rule_result->data);
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
		SET_TYPE("ident_data_p", ident_data);
		ident_data->ident[0] = ch;
		ident_data->len = 1;
	}
	else
	{
		result_assign(result, prev);
		ident_data_p ident_data = CAST(ident_data_p, result->data);
		if (ident_data->len < 64)
			ident_data->ident[ident_data->len++] = ch;
	}
	return TRUE;
}

void ident_set_pos(result_p result, text_pos_p ps)
{
	if (result->data != 0)
		(CAST(ident_data_p, result->data))->ps = *ps;
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
	bool is_keyword;
};

void ident_print(void *data, ostream_p ostream)
{
	ostream_puts(ostream, CAST(ident_p, data)->name);
}
const char *ident_type = "ident";

bool create_ident_tree(const result_p rule_result, void* data, result_p result)
{
	ident_data_p ident_data = CAST(ident_data_p, rule_result->data);
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
	ident->is_keyword = *keyword_state == 1;
	result_assign_ref_counted(result, ident, ident_print);
	SET_TYPE("ident_p", ident);
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
	ENTER_RESULT_CONTEXT
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
				ident_p ident = CAST(ident_p, tree_node);
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
	EXIT_RESULT_CONTEXT
}

void test_ident_grammar(non_terminal_dict_p *all_nt)
{
	test_parse_ident(all_nt, "aBc");
	test_parse_ident(all_nt, "_123");
}

/*
	Char result
	~~~~~~~~~~~

	The struct for representing the char has but two members,
	one for the character and one for the start position.
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
	print_single_char(CAST(char_data_p, data)->ch, ostream);
	ostream_puts(ostream, "'");
}

void char_set_pos(result_p result, text_pos_p ps)
{
	char_data_p char_data = MALLOC(struct char_data);
	char_data->ps = *ps;
	char_data->_base.release = 0;
	result_assign_ref_counted(result, char_data, char_data_print);
	SET_TYPE("char_data_p", char_data);
}

bool normal_char(result_p prev, char ch, result_p result)
{
	result_assign(result, prev);
	CAST(char_data_p, result->data)->ch = ch;
	return TRUE;
}

bool escaped_char(result_p prev, char ch, result_p result)
{
	return normal_char(prev,
		ch == '0' ? '\0' :
		ch == 'a' ? '\a' :
		ch == 'b' ? '\b' :
		ch == 'f' ? '\f' :
		ch == 'n' ? '\n' :
		ch == 'r' ? '\r' :
		ch == 't' ? '\t' :
		ch == 'v' ? '\v' : ch, result);
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
	char_data_p char_data = CAST(char_data_p, rule_result->data);

	char_node_p char_node = MALLOC(struct char_node_t);
	init_tree_node(&char_node->_node, char_node_type, NULL);
	tree_node_set_pos(&char_node->_node, &char_data->ps);
	char_node->ch = char_data->ch;
	result_assign_ref_counted(result, char_node, char_node_print);
	SET_TYPE("char_data_p", char_data);
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
					CHAR('\\') CHARSET(escaped_char) ADD_CHAR('0') ADD_CHAR('\"') ADD_CHAR('\'') ADD_CHAR('\\') ADD_CHAR('a') ADD_CHAR('b') ADD_CHAR('f') ADD_CHAR('n') ADD_CHAR('r') ADD_CHAR('t') ADD_CHAR('v')
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
	ENTER_RESULT_CONTEXT
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

	EXIT_RESULT_CONTEXT
}

void test_char_grammar(non_terminal_dict_p *all_nt)
{
	test_parse_char(all_nt, "'c'", 'c');
	test_parse_char(all_nt, "'\\0'", '\0');
	test_parse_char(all_nt, "'\\''", '\'');
	test_parse_char(all_nt, "'\\\\'", '\\');
	test_parse_char(all_nt, "'\\n'", '\n');
}

/*
	String result
	~~~~~~~~~~~~~
	
	The struct representing the string has several members.
	But we first need to define a string buffer type that is
	needed as a temporary storage while the string is parsed
	before memory is allocated to contain the whole string.
*/

typedef struct string_buffer *string_buffer_p;
struct string_buffer
{
	char buf[100];
	string_buffer_p next;
};
string_buffer_p global_string_buffer = NULL;

string_buffer_p new_string_buffer()
{
	string_buffer_p string_buffer = MALLOC(struct string_buffer);
	string_buffer->next = NULL;
	return string_buffer;
}

typedef struct string_data
{
	ref_counted_base_t _base;
	string_buffer_p buffer;
	size_t length;
	char octal_char;
	text_pos_t ps;
} *string_data_p;

void string_data_print(void *data, ostream_p ostream)
{
	string_data_p string_data = CAST(string_data_p, data);
	ostream_puts(ostream, "char \"");
	string_buffer_p string_buffer = global_string_buffer;
	int j = 0;
	for (size_t i = 0; i < string_data->length; i++)
	{
		if (++j > 100)
		{
			string_buffer = string_buffer->next;
			if (string_buffer == NULL)
				break;
			j = 0;
		}
		print_single_char(string_buffer->buf[j], ostream);
	}
	ostream_puts(ostream, "\"");
}

void string_set_pos(result_p result, text_pos_p ps)
{
	if (result->data == NULL)
	{
		string_data_p string_data = MALLOC(struct string_data);
		string_data->ps = *ps;
		string_data->buffer = NULL;
		string_data->length = 0;
		string_data->_base.release = 0;
		result_assign_ref_counted(result, string_data, string_data_print);
		SET_TYPE("string_data_p", string_data);
	}
}

bool string_data_add_normal_char(result_p prev, char ch, result_p result)
{
	result_assign(result, prev);
	string_data_p string_data = CAST(string_data_p, result->data);
	int j = string_data->length % 100;
	if (string_data->length == 0)
	{
		if (global_string_buffer == NULL)
			global_string_buffer = new_string_buffer();
		string_data->buffer = global_string_buffer;
	}
	else if (j == 0)
	{
		if (string_data->buffer->next == NULL)
			string_data->buffer->next = new_string_buffer();
		string_data->buffer = string_data->buffer->next;
	}
	string_data->buffer->buf[j++] = ch;
	string_data->length++;
	return TRUE;
}

bool string_data_add_escaped_char(result_p prev, char ch, result_p result)
{
	return string_data_add_normal_char(prev, ch == '0' ? '\0' : ch == 'n' ? '\n' : ch == 'r' ? '\r' : ch, result);
}

bool string_data_add_first_octal(result_p prev, char ch, result_p result)
{
	result_assign(result, prev);
	string_data_p string_data = CAST(string_data_p, result->data);
	string_data->octal_char = (ch - '0') << 6;
	return TRUE;
}

bool string_data_add_second_octal(result_p prev, char ch, result_p result)
{
	result_assign(result, prev);
	string_data_p string_data = CAST(string_data_p, result->data);
	string_data->octal_char |= (ch - '0') << 3;
	return TRUE;
}

bool string_data_add_third_octal(result_p prev, char ch, result_p result)
{
	string_data_p string_data = CAST(string_data_p, prev->data);
	return string_data_add_normal_char(prev, string_data->octal_char | (ch - '0'), result);
}

/*	String tree node structure */

typedef struct string_node_t *string_node_p;
struct string_node_t
{
	tree_node_t _node;
	const char *str;
};

const char *string_node_type = "string";

void string_node_print(void *data, ostream_p ostream)
{
	string_node_p string_node = CAST(string_node_p, data);
	ostream_puts(ostream, "string \"");
	for (const char *s = string_node->str; *s != '\0'; s++)
		print_single_char(*s, ostream);
	ostream_puts(ostream, "\"");
}

bool create_string_tree(const result_p rule_result, void* data, result_p result)
{
	string_data_p string_data = CAST(string_data_p, rule_result->data);
	
	string_node_p string_node = MALLOC(struct string_node_t);
	init_tree_node(&string_node->_node, string_node_type, NULL);
	tree_node_set_pos(&string_node->_node, &string_data->ps);
	char *s = MALLOC_N(string_data->length + 1, char);
	string_node->str = s;
	string_buffer_p string_buffer = global_string_buffer;
	int j = 0;
	for (size_t i = 0; i < string_data->length; i++)
	{
		if (string_buffer == NULL)
			break;
		*s++ = string_buffer->buf[j++];
		if (j > 100)
		{
			string_buffer = string_buffer->next;
			j = 0;
		}
	}
	*s = '\0';
	result_assign_ref_counted(result, string_node, string_node_print);
	SET_TYPE("string_node_p", string_node);
	return TRUE;
}
		
/*	String grammar */

void string_grammar(non_terminal_dict_p *all_nt)
{
	HEADER(all_nt)
	
	NT_DEF("string")
		RULE
			{ GROUPING
				RULE
					CHAR('"') SET_PS(string_set_pos)
					{ GROUPING
						RULE // Octal character
							CHAR('\\')
							CHARSET(string_data_add_first_octal) ADD_CHAR('0') ADD_CHAR('1')
							CHARSET(string_data_add_second_octal) ADD_RANGE('0','7')
							CHARSET(string_data_add_third_octal) ADD_RANGE('0','7')
						RULE // Escaped character
							CHAR('\\') CHARSET(string_data_add_escaped_char) ADD_CHAR('0') ADD_CHAR('\'') ADD_CHAR('"') ADD_CHAR('\\') ADD_CHAR('n') ADD_CHAR('r')
						RULE // Normal character
							CHARSET(string_data_add_normal_char) ADD_RANGE(' ', 126) REMOVE_CHAR('\\') REMOVE_CHAR('"')
					} SEQ(pass_to_sequence, use_sequence_result) OPT(0)
					CHAR('"')
			} SEQ(pass_to_sequence, use_sequence_result) { CHAIN NTF("white_space", 0) }
			END_FUNCTION(create_string_tree)
}

void test_parse_string(non_terminal_dict_p *all_nt, const char *input, const char *str)
{
	ENTER_RESULT_CONTEXT

	text_buffer_t text_buffer;
	text_buffer_assign_string(&text_buffer, input);
	
	solutions_t solutions;
	solutions_init(&solutions, &text_buffer);
	
	parser_t parser;
	parser_init(&parser, &text_buffer);
	parser.cache_hit_function = solutions_find;
	parser.cache = &solutions;
	
	DECL_RESULT(result);
	if (parse_nt(&parser, find_nt("string", all_nt), &result) && text_buffer_end(&text_buffer))
	{
		if (result.data == NULL)
			fprintf(stderr, "ERROR: parsing '%s' did not return result\n", input);
		else
		{
			tree_node_p tree_node = (tree_node_p)result.data;
			if (tree_node->line != 1 && tree_node->column != 1)
				fprintf(stderr, "WARNING: tree node position %d:%d is not 1:1\n", tree_node->line, tree_node->column);
			if (tree_node->type_name != string_node_type)
				fprintf(stderr, "ERROR: tree node is not of type string_node_type\n");
			else
			{
				string_node_p string_node = CAST(string_node_p, tree_node);
				if (strcmp(string_node->str, str) != 0)
					fprintf(stderr, "ERROR: parsed value '%s' from '%s' instead of expected '%s'\n",
					string_node->str, input, str);
				else
					fprintf(stderr, "OK: parsed string \"%s\" from \"%s\"\n", string_node->str, input);
			}
		}
	}
	else
	{
		fprintf(stderr, "ERROR: failed to parse string from '%s'\n", input);
	}
	DISP_RESULT(result);
	
	solutions_free(&solutions);

	EXIT_RESULT_CONTEXT
}

void test_string_grammar(non_terminal_dict_p *all_nt)
{
	test_parse_string(all_nt, "\"abc\"", "abc");
	test_parse_string(all_nt, "\"\\0\"", "");
	test_parse_string(all_nt, "\"\\'\"", "\'");
	test_parse_string(all_nt, "\"abc\" /* */ \"def\"", "abcdef");
	test_parse_string(all_nt, "\"\\n\"", "\n");
}

/*
	Int result
	~~~~~~~~~~
	
	For parsing an integer value a single function will used,
	which will implement a co-routine for processing the
	characters.
*/

typedef struct int_data
{
	ref_counted_base_t _base;
	long long int value;
	int state;
	int sign;
	text_pos_t ps;
} *int_data_p;

void int_data_print(void *data, ostream_p ostream)
{
	int_data_p int_data = CAST(int_data_p, data);
	char buffer[51];
	snprintf(buffer, 50, "int %lld", int_data->sign * int_data->value);
	buffer[50] = '\0';
	ostream_puts(ostream, buffer);
}

void int_set_pos(result_p result, text_pos_p ps)
{
	if (result->data != NULL && CAST(int_data_p, result->data)->ps.cur_line == -1)
		CAST(int_data_p, result->data)->ps = *ps;
}

#define INT_DATA_WAIT_NEXT_CHAR(X)  int_data->state = X; return TRUE; L##X:

bool int_data_add_char(result_p prev, char ch, result_p result)
{
	if (prev->data == NULL)
	{
		int_data_p int_data = MALLOC(struct int_data);
		int_data->value = 0;
		int_data->state = 0;
		int_data->sign = 1;
		int_data->_base.release = 0;
		int_data->ps.cur_line = -1;
		result_assign_ref_counted(result, int_data, int_data_print);
		SET_TYPE("int_data_p", int_data);
	}
	else
		result_assign(result, prev);
	int_data_p int_data = CAST(int_data_p, result->data);
	
	switch (int_data->state)
	{
		case 0: goto L0;
		case 1: goto L1;
		case 2: goto L2;
		case 3: goto L3;
		case 4: goto L4;
		case 5: goto L5;
		case 6: goto L6;
	}
	L0:
	
	if (ch == '-')
	{
		int_data->sign = -1;
		INT_DATA_WAIT_NEXT_CHAR(1)
	}
	if (ch == '0')
	{
		INT_DATA_WAIT_NEXT_CHAR(2)
		if (ch == 'x')
		{
			// Process hexa decimal number
			INT_DATA_WAIT_NEXT_CHAR(3)
			for (;;)
			{
				if ('0' <= ch && ch <= '9')
					int_data->value = 16 * int_data->value + ch - '0';
				else if ('A' <= ch && ch <= 'F')
					int_data->value = 16 * int_data->value + ch + (10 - 'A');
				else if ('a' <= ch && ch <= 'f')
					int_data->value = 16 * int_data->value + ch + (10 - 'a');
				else
					break;
				INT_DATA_WAIT_NEXT_CHAR(4)
			}
		}
		else
		{
			// Process octal number
			while ('0' <= ch && ch <= '7')
			{
				int_data->value = 8 * int_data->value +  ch - '0';
				INT_DATA_WAIT_NEXT_CHAR(5)
			}
		}
	}
	else
	{
		// Process decimal number
		while ('0' <= ch && ch <= '9')
		{
			int_data->value = 10 * int_data->value + ch - '0';
			INT_DATA_WAIT_NEXT_CHAR(6)
		}
	}
	return FALSE;
}

/*	Int tree node structure */

typedef struct int_node_t *int_node_p;
struct int_node_t
{
	tree_node_t _node;
	long long int value;
};

const char *int_node_type = "int";

void int_node_print(void *data, ostream_p ostream)
{
	int_node_p int_node = (int_node_p)data;
	char buffer[51];
	snprintf(buffer, 50, "int %lld", int_node->value);
	buffer[50] = '\0';
	ostream_puts(ostream, buffer);
}

bool create_int_tree(const result_p rule_result, void* data, result_p result)
{
	int_data_p int_data = CAST(int_data_p, rule_result->data);
	
	int_node_p int_node = MALLOC(struct int_node_t);
	init_tree_node(&int_node->_node, int_node_type, NULL);
	tree_node_set_pos(&int_node->_node, &int_data->ps);
	int_node->value = int_data->sign * int_data->value;
	result_assign_ref_counted(result, int_node, int_node_print);
	SET_TYPE("int_data_p", int_data);
	return TRUE;
}
		
/*	Int grammar */

void int_grammar(non_terminal_dict_p *all_nt)
{
	HEADER(all_nt)
	
	NT_DEF("int")
		RULE
			CHARF('-', int_data_add_char) OPT(0) SET_PS(int_set_pos)
			{ GROUPING
				RULE // hexadecimal representaion
					CHARF('0', int_data_add_char) SET_PS(int_set_pos) 
					CHARF('x', int_data_add_char)
					CHARSET(int_data_add_char) ADD_RANGE('0','9') ADD_RANGE('A','F') ADD_RANGE('a','f') SEQ(pass_to_sequence, use_sequence_result)
				RULE // octal representation
					CHARF('0', int_data_add_char) SET_PS(int_set_pos) 
					CHARSET(int_data_add_char) ADD_RANGE('0','7') SEQ(pass_to_sequence, use_sequence_result) OPT(0)
				RULE // decimal representation
					CHARSET(int_data_add_char) ADD_RANGE('1','9') SET_PS(int_set_pos) 
					CHARSET(int_data_add_char) ADD_RANGE('0','9') SEQ(pass_to_sequence, use_sequence_result) OPT(0)
 			}
			CHAR('U') OPT(0)
			CHAR('L') OPT(0)
			CHAR('L') OPT(0)
			END_FUNCTION(create_int_tree)
}

void test_parse_int(non_terminal_dict_p *all_nt, const char *input, long long int value)
{
	ENTER_RESULT_CONTEXT

	text_buffer_t text_buffer;
	text_buffer_assign_string(&text_buffer, input);
	
	solutions_t solutions;
	solutions_init(&solutions, &text_buffer);
	
	parser_t parser;
	parser_init(&parser, &text_buffer);
	parser.cache_hit_function = solutions_find;
	parser.cache = &solutions;
	
	DECL_RESULT(result);
	if (parse_nt(&parser, find_nt("int", all_nt), &result) && text_buffer_end(&text_buffer))
	{
		if (result.data == NULL)
			fprintf(stderr, "ERROR: parsing '%s' did not return result\n", input);
		else
		{
			tree_node_p tree_node = (tree_node_p)result.data;
			if (tree_node->line != 1 && tree_node->column != 1)
				fprintf(stderr, "WARNING: tree node position %d:%d is not 1:1\n", tree_node->line, tree_node->column);
			if (tree_node->type_name != int_node_type)
				fprintf(stderr, "ERROR: tree node is not of type int_node_type\n");
			else
			{
				int_node_p int_node = (int_node_p)tree_node;
				if (int_node->value != value)
					fprintf(stderr, "ERROR: parsed value %lld from '%s' instead of expected %lld\n",
					int_node->value, input, value);
				else
					fprintf(stderr, "OK: parsed integer %lld from \"%s\"\n", int_node->value, input);
			}
		}
	}
	else
	{
		fprintf(stderr, "ERROR: failed to parse int from '%s'\n", input);
	}
	DISP_RESULT(result);
	
	solutions_free(&solutions);

	EXIT_RESULT_CONTEXT
}

void test_int_grammar(non_terminal_dict_p *all_nt)
{
	test_parse_int(all_nt, "0", 0);
	test_parse_int(all_nt, "1", 1);
	test_parse_int(all_nt, "-1", -1);
	test_parse_int(all_nt, "077", 077);
	test_parse_int(all_nt, "0xAbc", 0xAbc);
	test_parse_int(all_nt, "1234L", 1234);
	test_parse_int(all_nt, "-23", -23);
	test_parse_int(all_nt, "46464664", 46464664);
}




bool equal_string(result_p result, const void *argument)
{
	const char *keyword_name = (const char*)argument;
	return    result->data != NULL
		   && ((tree_node_p)result->data)->type_name == ident_type
		   && strcmp(CAST(ident_p, result->data)->name, keyword_name) == 0;
}

bool not_a_keyword(result_p result, const void *argument)
{
	ident_p ident = CAST(ident_p, result->data);
	return !ident->is_keyword;
}

const char* list_type = "list";

bool add_seq_as_list(result_p prev, result_p seq, result_p result)
{
	prev_child_p new_prev_child = malloc_prev_child();
	new_prev_child->prev = CAST(prev_child_p, prev->data);
	tree_p list = make_tree_with_children(list_type, CAST(prev_child_p, seq->data));
	result_assign_ref_counted(&new_prev_child->child, list, tree_print);
	SET_TYPE("tree_p", list);
	result_assign_ref_counted(result, new_prev_child, NULL);
	SET_TYPE("prev_child_p", new_prev_child);
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
#define IDENT_OPT NTF("ident", add_child) element->condition = not_a_keyword; OPTN WS
#define SEQL SEQ(0, add_seq_as_list)
#define REC_RULEC REC_RULE(rec_add_child);
#define CHAR_WS(C) CHAR(C) WS

void c_grammar(non_terminal_dict_p *all_nt)
{
	white_space_grammar(all_nt);
	ident_grammar(all_nt);
	
	HEADER(all_nt)
	
	NT_DEF("primary_expr")
		RULE IDENT PASS
		RULE NTP("int") WS
		RULE NTP("double") WS
		RULE NTP("char") WS
		RULE NTP("string") WS
		RULE CHAR_WS('(') NTP("expr") CHAR_WS(')')

	NT_DEF("postfix_expr")
		RULE NTP("primary_expr")
		REC_RULEC CHAR_WS('[') NT("expr") CHAR_WS(']') TREE("arrayexp")
		REC_RULEC CHAR_WS('(') NT("assignment_expr") SEQL { CHAIN CHAR_WS(',') } OPTN CHAR_WS(')') TREE("call")
		REC_RULEC CHAR_WS('.') IDENT TREE("field")
		REC_RULEC CHAR('-') CHAR_WS('>') IDENT TREE("fieldderef")
		REC_RULEC CHAR('+') CHAR_WS('+') TREE("post_inc")
		REC_RULEC CHAR('-') CHAR_WS('-') TREE("post_dec")

	NT_DEF("unary_expr")
		RULE CHAR('+') CHAR_WS('+') NT("unary_expr") TREE("pre_inc")
		RULE CHAR('-') CHAR_WS('-') NT("unary_expr") TREE("pre_dec")
		RULE CHAR_WS('&') NT("cast_expr") TREE("address_of")
		RULE CHAR_WS('*') NT("cast_expr") TREE("deref")
		RULE CHAR_WS('+') NT("cast_expr") TREE("plus")
		RULE CHAR_WS('-') NT("cast_expr") TREE("min")
		RULE CHAR_WS('~') NT("cast_expr") TREE("invert")
		RULE CHAR_WS('!') NT("cast_expr") TREE("not")
		RULE KEYWORD("sizeof")
		{ GROUPING
			RULE CHAR_WS('(') NT("sizeof_type") CHAR_WS(')') TREE("sizeof")
			RULE NT("unary_expr") TREE("sizeof_expr")
		}
		RULE NTP("postfix_expr")

	NT_DEF("sizeof_type")
		RULE KEYWORD("char") TREE("char")
		RULE KEYWORD("short") TREE("short")
		RULE KEYWORD("int") TREE("int")
		RULE KEYWORD("long") TREE("long")
		RULE KEYWORD("signed") NT("sizeof_type") TREE("signed")
		RULE KEYWORD("unsigned") NT("sizeof_type") TREE("unsigned")
		RULE KEYWORD("float") TREE("float")
		RULE KEYWORD("double") NT("sizeof_type") OPTN TREE("double")
		RULE KEYWORD("const") NT("sizeof_type") TREE("const")
		RULE KEYWORD("volatile") NT("sizeof_type") TREE("volatile")
		RULE KEYWORD("void") TREE("void")
		RULE KEYWORD("struct") IDENT TREE("structdecl")
		RULE IDENT
		REC_RULEC WS CHAR_WS('*') TREE("pointdecl")

	NT_DEF("cast_expr")
		RULE CHAR_WS('(') NT("abstract_declaration") CHAR_WS(')') NT("cast_expr") TREE("cast")
		RULE NTP("unary_expr")

	NT_DEF("l_expr1")
		RULE NTP("cast_expr")
		REC_RULEC WS CHAR_WS('*') NT("cast_expr") TREE("times")
		REC_RULEC WS CHAR_WS('/') NT("cast_expr") TREE("div")
		REC_RULEC WS CHAR_WS('%') NT("cast_expr") TREE("mod")

	NT_DEF("l_expr2")
		RULE NTP("l_expr1")
		REC_RULEC WS CHAR_WS('+') NT("l_expr1") TREE("add")
		REC_RULEC WS CHAR_WS('-') NT("l_expr1") TREE("sub")

	NT_DEF("l_expr3")
		RULE NTP("l_expr2")
		REC_RULEC WS CHAR('<') CHAR_WS('<') NT("l_expr2") TREE("ls")
		REC_RULEC WS CHAR('>') CHAR_WS('>') NT("l_expr2") TREE("rs")

	NT_DEF("l_expr4")
		RULE NTP("l_expr3")
		REC_RULEC WS CHAR('<') CHAR_WS('=') NT("l_expr3") TREE("le")
		REC_RULEC WS CHAR('>') CHAR_WS('=') NT("l_expr3") TREE("ge")
		REC_RULEC WS CHAR_WS('<') NT("l_expr3") TREE("lt")
		REC_RULEC WS CHAR_WS('>') NT("l_expr3") TREE("gt")
		REC_RULEC WS CHAR('=') CHAR_WS('=') NT("l_expr3") TREE("eq")
		REC_RULEC WS CHAR('!') CHAR_WS('=') NT("l_expr3") TREE("ne")

	NT_DEF("l_expr5")
		RULE NTP("l_expr4")
		REC_RULEC WS CHAR_WS('^') NT("l_expr4") TREE("bexor")

	NT_DEF("l_expr6")
		RULE NTP("l_expr5")
		REC_RULEC WS CHAR_WS('&') NT("l_expr5") TREE("land")

	NT_DEF("l_expr7")
		RULE NTP("l_expr6")
		REC_RULEC WS CHAR_WS('|') NT("l_expr6") TREE("lor")

	NT_DEF("l_expr8")
		RULE NTP("l_expr7")
		REC_RULEC WS CHAR('&') CHAR_WS('&') NT("l_expr7") TREE("and")

	NT_DEF("l_expr9")
		RULE NTP("l_expr8")
		REC_RULEC WS CHAR('|') CHAR_WS('|') NT("l_expr8") TREE("or")

	NT_DEF("conditional_expr")
		RULE NT("l_expr9") WS CHAR_WS('?') NT("l_expr9") WS CHAR_WS(':') NT("conditional_expr") TREE("if_expr")
		RULE NTP("l_expr9")

	NT_DEF("assignment_expr")
		RULE NT("unary_expr") WS NT("assignment_operator") WS NT("assignment_expr") TREE("assignment")
		RULE NTP("conditional_expr")

	NT_DEF("assignment_operator")
		RULE CHAR_WS('=') TREE("ass")
		RULE CHAR('*') CHAR_WS('=') TREE("times_ass")
		RULE CHAR('/') CHAR_WS('=') TREE("div_ass")
		RULE CHAR('%') CHAR_WS('=') TREE("mod_ass")
		RULE CHAR('+') CHAR_WS('=') TREE("add_ass")
		RULE CHAR('-') CHAR_WS('=') TREE("sub_ass")
		RULE CHAR('<') CHAR('<') CHAR_WS('=') TREE("sl_ass")
		RULE CHAR('>') CHAR('>') CHAR_WS('=') TREE("sr_ass")
		RULE CHAR('&') CHAR_WS('=') TREE("and_ass")
		RULE CHAR('|') CHAR_WS('=') TREE("or_ass")
		RULE CHAR('^') CHAR_WS('=') TREE("exor_ass")

	NT_DEF("expr")
		RULE NT("assignment_expr") SEQL { CHAIN CHAR_WS(',') } PASS

	NT_DEF("constant_expr")
		RULE NT("conditional_expr") PASS

	NT_DEF("declaration")
		RULE
		{ GROUPING
			RULE NT("storage_class_specifier")
			RULE NT("type_specifier")
		} SEQL OPTN AVOID
		{ GROUPING
			RULE NT("func_declarator") CHAR_WS('(')
			{ GROUPING
				RULE NT("parameter_declaration_list") OPTN
				RULE KEYWORD("void") TREE("void")
			}
			CHAR_WS(')')
			{ GROUPING
				RULE CHAR_WS(';')
				RULE CHAR_WS('{') NT("decl_or_stat") CHAR_WS('}')
			} TREE("new_style") WS
			RULE NT("func_declarator") CHAR_WS('(') NT("ident_list") OPTN CHAR_WS(')') NT("declaration") SEQL OPTN CHAR_WS('{') NT("decl_or_stat") CHAR_WS('}') TREE("old_style")
			RULE
			{ GROUPING
				RULE NT("declarator")
				{ GROUPING
					RULE WS CHAR_WS('=') NT("initializer")
				} OPTN
			} SEQL { CHAIN CHAR_WS(',') } OPTN CHAR_WS(';') TREE("decl")
		}

	NT_DEF("storage_class_specifier")
		RULE KEYWORD("typedef") TREE("typedef")
		RULE KEYWORD("extern") TREE("extern")
		RULE KEYWORD("inline") TREE("inline")
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
		RULE KEYWORD("struct") IDENT CHAR_WS('{')
		{ GROUPING
			RULE NT("struct_declaration_or_anon")
		} SEQL CHAR_WS('}') TREE("struct_d")
		RULE KEYWORD("struct") CHAR_WS('{')
		{ GROUPING
			RULE NT("struct_declaration_or_anon")
		} SEQL CHAR_WS('}') TREE("struct_n")
		RULE KEYWORD("struct") IDENT TREE("struct")
		RULE KEYWORD("union") IDENT CHAR_WS('{')
		{ GROUPING
			RULE NT("struct_declaration_or_anon")
		} SEQL CHAR_WS('}') TREE("union_d")
		RULE KEYWORD("union") CHAR_WS('{')
		{ GROUPING
			RULE NT("struct_declaration_or_anon")
		} SEQL CHAR_WS('}') TREE("union_n")
		RULE KEYWORD("union") IDENT TREE("union")

	NT_DEF("struct_declaration_or_anon")
		RULE NT("struct_or_union_specifier") CHAR_WS(';')
		RULE NT("struct_declaration")

	NT_DEF("struct_declaration")
		RULE NT("type_specifier") NT("struct_declaration") TREE("type")
		RULE NT("struct_declarator") SEQL { CHAIN CHAR_WS(',') } CHAR_WS(';') TREE("strdec")

	NT_DEF("struct_declarator")
		RULE NT("declarator")
		{ GROUPING
			RULE CHAR_WS(':') NT("constant_expr")
		} OPTN TREE("record_field")

	NT_DEF("enum_specifier")
		RULE KEYWORD("enum") IDENT_OPT
		{ GROUPING
			RULE CHAR_WS('{') NT("enumerator") SEQL { CHAIN CHAR_WS(',') } CHAR_WS('}')
		} TREE("enum")

	NT_DEF("enumerator")
		RULE IDENT
		{ GROUPING
			RULE CHAR_WS('=') NT("constant_expr")
		} OPTN TREE("enumerator")

	NT_DEF("func_declarator")
		RULE CHAR_WS('*')
		{ GROUPING
			RULE KEYWORD("const") TREE("const")
		} OPTN NT("func_declarator") TREE("pointdecl")
		RULE CHAR_WS('(') NT("func_declarator") CHAR_WS(')')
		RULE IDENT

	NT_DEF("declarator")
		RULE CHAR_WS('*')
		{ GROUPING
			RULE KEYWORD("const") TREE("const")
		} OPTN NT("declarator") TREE("pointdecl")
		RULE CHAR_WS('(') NT("declarator") CHAR_WS(')') TREE("brackets")
		RULE WS IDENT
		REC_RULEC CHAR_WS('[') NT("constant_expr") OPTN CHAR_WS(']') TREE("array")
		REC_RULEC CHAR_WS('(') NT("abstract_declaration_list") OPTN CHAR_WS(')') TREE("function")

	NT_DEF("abstract_declaration_list")
		RULE NT("abstract_declaration")
		{ GROUPING
			RULE CHAR_WS(',')
			{ GROUPING
				RULE CHAR('.') CHAR('.') CHAR_WS('.') TREE("varargs")
				RULE NT("abstract_declaration_list")
			}
		} OPTN

	NT_DEF("parameter_declaration_list")
		RULE NT("parameter_declaration")
		{ GROUPING
			RULE CHAR_WS(',')
			{ GROUPING
				RULE CHAR('.') CHAR('.') CHAR_WS('.') TREE("varargs")
				RULE NT("parameter_declaration_list")
			}
		} OPTN

	NT_DEF("ident_list")
		RULE IDENT
		{ GROUPING
			RULE CHAR_WS(',')
			{ GROUPING
				RULE CHAR('.') CHAR('.') CHAR_WS('.') TREE("varargs")
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
		RULE CHAR_WS('*')
		{ GROUPING
			RULE KEYWORD("const") TREE("const")
		} OPTN NT("abstract_declarator") TREE("abs_pointdecl")
		RULE CHAR_WS('(') NT("abstract_declarator") CHAR_WS(')') TREE("abs_brackets")
		RULE
		REC_RULEC CHAR_WS('[') NT("constant_expr") OPTN CHAR_WS(']') TREE("abs_array")
		REC_RULEC CHAR_WS('(') NT("parameter_declaration_list") CHAR_WS(')') TREE("abs_func")

	NT_DEF("initializer")
		RULE NT("assignment_expr")
		RULE CHAR_WS('{') NT("initializer") SEQL { CHAIN CHAR_WS(',') } CHAR(',') OPTN WS CHAR_WS('}') TREE("initializer")

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
			} CHAR_WS(':') NT("statement") TREE("label")
			RULE CHAR_WS('{') NT("decl_or_stat") CHAR_WS('}') TREE("brackets")
		}
		RULE
		{ GROUPING
			RULE NT("expr") OPTN CHAR_WS(';')
			RULE KEYWORD("if") WS CHAR_WS('(') NT("expr") CHAR_WS(')') NT("statement")
			{ GROUPING
				RULE KEYWORD("else") NT("statement")
			} OPTN TREE("if")
			RULE KEYWORD("switch") WS CHAR_WS('(') NT("expr") CHAR_WS(')') NT("statement") TREE("switch")
			RULE KEYWORD("while") WS CHAR_WS('(') NT("expr") CHAR_WS(')') NT("statement") TREE("while")
			RULE KEYWORD("do") NT("statement") KEYWORD("while") WS CHAR_WS('(') NT("expr") CHAR_WS(')') CHAR_WS(';') TREE("do")
			RULE KEYWORD("for") WS CHAR_WS('(') NT("expr") OPTN CHAR_WS(';')
			{ GROUPING
				RULE WS NT("expr")
			} OPTN CHAR_WS(';')
			{ GROUPING
				RULE WS NT("expr")
			} OPTN CHAR_WS(')') NT("statement") TREE("for")
			RULE KEYWORD("goto") IDENT CHAR_WS(';') TREE("goto")
			RULE KEYWORD("continue") CHAR_WS(';') TREE("cont")
			RULE KEYWORD("break") CHAR_WS(';') TREE("break")
			RULE KEYWORD("return") NT("expr") OPTN CHAR_WS(';') TREE("ret")
		}

	NT_DEF("root")
		RULE
		WS
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
	ENTER_RESULT_CONTEXT

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

	EXIT_RESULT_CONTEXT
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

/*
	Expect
*/

#define MAX_EXP_SYM 200

struct nt_stack
{
	const char *name;
	unsigned long int ref_count;
	text_pos_t pos;
	nt_stack_p parent;
};
nt_stack_p nt_stack_allocated = NULL;

nt_stack_p nt_stack_push(const char *name, parser_p parser)
{
	nt_stack_p child;
	if (nt_stack_allocated != NULL)
	{
		child = nt_stack_allocated;
		nt_stack_allocated = child->parent;
	}
	else
		child = MALLOC(struct nt_stack);
	child->name = name;
	child->ref_count = 1;
	child->pos = parser->text_buffer->pos;
	child->parent = parser->nt_stack;
	if (parser->nt_stack != 0)
		parser->nt_stack->ref_count++;
	//DEBUG_TAB; DEBUG_P1("push %s\n", child->name);
	return child;
}

void nt_stack_dispose(nt_stack_p nt_stack)
{
	while (nt_stack != NULL && --nt_stack->ref_count == 0)
	{
		nt_stack_p parent = nt_stack->parent;
		nt_stack->parent = nt_stack_allocated;
		nt_stack_allocated = nt_stack;
		nt_stack = parent;
	}
}

nt_stack_p nt_stack_pop(nt_stack_p cur)
{
	//DEBUG_TAB; DEBUG_P1("pop %s\n", cur == NULL ? "<NULL>" : cur->name);
	nt_stack_p parent = cur->parent;
	nt_stack_dispose(cur);
	return parent;
}

text_pos_t highest_pos;
typedef struct
{
	nt_stack_p nt_stack;
	element_p element;
} expect_t;
expect_t expected[MAX_EXP_SYM];
int nr_expected;

void init_expected()
{
	highest_pos.pos = 0;
	nr_expected = 0;
}

void expect_element(parser_p parser, element_p element)
{
	if (parser->text_buffer->pos.pos < highest_pos.pos) return;
	
	if (parser->text_buffer->pos.pos > highest_pos.pos)
	{
		highest_pos = parser->text_buffer->pos;
		//for (int i = 0; i < nr_expected; i++)
		//	nt_stack_dispose(expected[i].nt_stack);
		nr_expected = 0;
	}
	for (int i = 0; i < nr_expected; i++)
		if (expected[i].nt_stack == parser->nt_stack && expected[i].element == element)
			return;
	if (nr_expected < MAX_EXP_SYM)
	{
		parser->nt_stack->ref_count++;
		expected[nr_expected].nt_stack = parser->nt_stack;
		expected[nr_expected].element = element;
		nr_expected++;
	}
}



void print_expected(FILE *fout)
{
	fprintf(fout, "Expect at %d.%d:\n", highest_pos.cur_line, highest_pos.cur_column);
	for (int i = 0; i < nr_expected; i++)
	{
		element_p element = expected[i].element;
		fprintf(fout, "- expect ");
		element_print(fout, element);
		fprintf(fout, "\n");
		for (nt_stack_p nt_stack = expected[i].nt_stack; nt_stack != NULL; nt_stack = nt_stack->parent)
			fprintf(fout, "  in %s at %d.%d\n", nt_stack->name, nt_stack->pos.cur_line, nt_stack->pos.cur_column);
	}
}


#ifndef INCLUDED

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
	
	string_grammar(&all_nt);
	test_string_grammar(&all_nt);
	
	int_grammar(&all_nt);
	test_int_grammar(&all_nt);
	
	non_terminal_dict_p all_nt_c_grammar = NULL;
	c_grammar(&all_nt_c_grammar);
    test_c_grammar(&all_nt_c_grammar);

	return 0;
}

#endif
