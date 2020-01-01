/* RawParser -- A 'raw' parser         Copyright (C) 2019 Frans Faase

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

#define VERSION "0.1 of December 2019."

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

typedef unsigned long longword;
typedef unsigned short word;
typedef unsigned char byte;

#define MALLOC(T) (T*)malloc(sizeof(T))
#define MALLOC_N(N,T)  (T*)malloc((N)*sizeof(T))
#define STR_MALLOC(N) (char*)malloc((N)+1)
#define STRCPY(D,S) D = (char*)malloc(strlen(S)+1); strcpy(D,S)
#define FREE(X) free(X)


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
	non-terminal has two list of alternatives (one for non-left recursive rules
	and one for left recursive rules). Each alternative defines a rule, which
	consists of list of grammar elements. An element can be one of:
	- character,
	- character set,
	- end of text,
	- non-terminal, or
	- grouping of alternatives.
	An element can have modifiers for making the element optional or an
	sequence. It is also possible to specify that an optional and/or
	sequential element should be avoided in favour of the remaining rule.
	With a sequential element it is possible to define a chain rule, which is
	to appear between the elements.
	Each element has a number of function pointers, which can be used to
	specify functions which should be called to process the parsing results.
	Furthermore, each alternative has a function pointer, to specify the
	function that should be called at the end of the rule to process the
	result.
	
	An example for a white space grammar will follow.
*/

/*  Forward declarations of types to be defined later.  */

typedef struct non_terminal non_terminal_t, *non_terminal_p;
typedef struct alternative *alternative_p;
typedef struct element *element_p;
typedef struct char_set char_set_t, *char_set_p;
typedef struct result result_t, *result_p;
typedef struct text_pos text_pos_t, *text_pos_p;

/*  Definition for a non-terminal  */

struct non_terminal
{
	const char *name;         /* Name of the non-terminal */
	alternative_p first;      /* Normal alternatives */
	alternative_p recursive;  /* Left-recursive alternatives */
	non_terminal_p next;      /* Next non-terminal (in the list) */
};

non_terminal_p find_nt(char *name, non_terminal_p *p_nt)
{
   while (*p_nt != NULL && (*p_nt)->name != name && strcmp((*p_nt)->name, name) != 0)
		p_nt = &((*p_nt)->next);

   if (*p_nt == NULL)
   {   *p_nt = MALLOC(non_terminal_t);
	   (*p_nt)->name = name;
	   (*p_nt)->first = NULL;
	   (*p_nt)->recursive = NULL;
	   (*p_nt)->next = NULL;
   }
   return *p_nt;
}

/*  Definition of an alternative  */

typedef bool (*end_function_p)(const result_p rule_result, result_p result);

struct alternative
{
	element_p rule;               /* The rule definition */
	end_function_p end_function;  /* Function pointer */
	alternative_p next;           /* Next alternative */
};

alternative_p new_alternative()
{
	alternative_p alternative = MALLOC(struct alternative);
	alternative->rule = NULL;
	alternative->end_function = NULL;
	alternative->next = NULL;
	return alternative;
}

/*  Defintion of an element of a rule  */

enum element_kind_t { rk_nt, rk_grouping, rk_char, rk_charset, rk_end };

struct element
{
	enum element_kind_t kind;
	bool optional;
	bool sequence;
	bool avoid;
	element_p chain_rule;
	union 
	{   non_terminal_p non_terminal;
		alternative_p alternative;
		char ch;
		char_set_p char_set;
	} info;
	bool (*condition)(result_p result, const void *argument);
	const void *condition_argument;
	bool (*add_function)(result_p prev, result_p elem, result_p result);
	bool (*add_char_function)(result_p prev, char ch, result_p result);
	bool (*add_seq_function)(result_p prev, result_p seq, result_p result);
	bool (*add_skip_function)(result_p prev, result_p result);
	void (*set_pos)(result_p result, text_pos_p ps);
	void (*begin_seq_function)(result_p prev, result_p seq);
	element_p next;
};

element_p new_element(enum element_kind_t kind)
{
	element_p element = MALLOC(struct element);
	element->next = NULL;
	element->optional = FALSE;
	element->sequence = FALSE;
	element->avoid = FALSE;
	element->chain_rule = NULL;
	element->kind = kind;
	element->condition = NULL;
	element->condition_argument = NULL;
	element->add_function = NULL;
	element->add_char_function = NULL;
	element->add_seq_function = NULL;
	element->add_skip_function = NULL;
	element->set_pos = NULL;
	element->begin_seq_function = NULL;
	return element;
}

/*  Definition of a character set (as a bit vector)  */

struct char_set
{
	byte bitvec[32];
};

bool char_set_contains(char_set_p char_set, const char ch) { return (char_set->bitvec[((byte)ch) >> 3] & (1 << (((byte)ch) & 0x7))) != 0; }
void char_set_add_char(char_set_p char_set, char ch) { char_set->bitvec[((byte)ch) >> 3] |= 1 << (((byte)ch) & 0x7); }
void char_set_add_range(char_set_p char_set, char first, char last)
{
	byte ch = (byte)first;
	for (; ((byte)first) <= ch && ch <= ((byte)last); ch++)
		char_set_add_char(char_set, ch);
}


/*  Printing routines for the internal representation  */

void element_print(FILE *f, element_p element);

void alternative_print(FILE *f, alternative_p alternative)
{
	bool first = TRUE;

	for (; alternative; alternative = alternative->next)
	{   
		if (!first)
			fprintf(f, "|");
		first = FALSE;
		element_print(f, alternative->rule);
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
			alternative_print(f, element->info.alternative);
			fprintf(f, ")");
			break;
		case rk_char:
			fprintf(f, "'%c' ", element->info.ch);
			break;
		case rk_charset:
			fprintf(f, "cs ");
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
	{   fprintf(f, "OPT ");
	}
	element_print(f, element->next);
}

/*  Some macro definitions for defining a grammar more easily.  */

#define HEADER(N) non_terminal_p *_nt = all_nt; non_terminal_p nt; alternative_p* ref_alternative; alternative_p* ref_rec_alternative; alternative_p alternative; element_p* ref_element; element_p element;
#define NT_DEF(N) nt = find_nt(N, _nt); ref_alternative = &nt->first; ref_rec_alternative = &nt->recursive;
#define RULE alternative = *ref_alternative = new_alternative(); ref_alternative = &alternative->next; ref_element = &alternative->rule;
#define REC_RULE alternative = *ref_rec_alternative = ; ref_rec_alternative = &alternative->next; ref_element = &alternative->rule;
#define _NEW_GR(K) element = *ref_element = new_element(K); ref_element = &element->next;
#define NT(N,F) _NEW_GR(rk_nt) element->info.non_terminal = find_nt(N, _nt); element->add_function = F;
#define END _NEW_GR(rk_end)
#define SEQ(S,E) element->sequence = TRUE; element->begin_seq_function = S; element->add_seq_function = E;
#define OPT(F) element->optional = TRUE; element->add_skip_function = F;
#define AVOID element->avoid = TRUE;
#define CHAR(C,F) _NEW_GR(rk_char) element->info.ch = C; element->add_char_function = F;
#define CHARSET(F) _NEW_GR(rk_charset) element->info.char_set = MALLOC(char_set_t); element->add_char_function = F;
#define ADD_CHAR(C) char_set_add_char(element->info.char_set, C);
#define ADD_RANGE(F,T) char_set_add_range(element->info.char_set, F, T);
#define END_FUNCTION(F) alternative->end_function = F;
#define OPEN _NEW_GR(rk_grouping) element->info.alternative = new_alternative(); { alternative_p* ref_alternative = &element->info.alternative; alternative_p alternative; element_p* ref_element; element_p element;
#define CLOSE }
		


/*
	Example of defining white space grammar with comments
	~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	In this example, white space does not have a result, thus all function
	pointers can be left 0. White space is defined as a (possible empty)
	sequence of white space characters, the single line comment and the
	traditional C-comment. 'OPEN' and 'CLOSE' are used to define a grouping.
	The grouping contains three rules.
*/

void white_space_grammar(non_terminal_p *all_nt)
{
	HEADER(all_nt)
	
	NT_DEF("white_space")
		RULE
			OPEN
				RULE /* for the usual white space characters */
					CHARSET(0) ADD_CHAR(' ') ADD_CHAR('\t') ADD_CHAR('\n')
				RULE /* for the single line comment starting with two slashes */
					CHAR('/', 0)
					CHAR('/', 0)
					CHARSET(0) ADD_RANGE(' ', 255) ADD_CHAR('\t') SEQ(0, 0) OPT(0)
					CHAR('\n', 0)
				RULE /* for the traditional C-comment (using avoid modifier) */
					CHAR('/', 0)
					CHAR('*', 0)
					CHARSET(0) ADD_RANGE(' ', 255) ADD_CHAR('\t') ADD_CHAR('\n') SEQ(0, 0) OPT(0) AVOID
					CHAR('*', 0)
					CHAR('/', 0)
			CLOSE SEQ(0, 0) OPT(0)
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

void number_grammar(non_terminal_p *all_nt)
{
	HEADER(all_nt)
	
	NT_DEF("number")
		RULE
			CHARSET(number_add_char) ADD_RANGE('0', '9') SEQ(0, use_sequence_result)
}

/*
	To implement the two functions, some definitions are needed, which
	will be explained below. 


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
	void (*print)(void *data, FILE *fout);
};

void result_init(result_p result)
{
	result->data = NULL;
	result->inc = 0;
	result->dec = 0;
	result->print = 0;
}

void result_assign(result_p trg, result_p src)
{
	result_t old_trg = *trg;
	if (src->inc != 0)
		src->inc(src->data);
	*trg = *src;
	if (old_trg.dec != 0)
		old_trg.dec(old_trg.data);
}

void result_release(result_p result)
{
	if (result->dec != 0)
		result->dec(result->data);
	result->data = NULL;
}

/*	Function for using result of a sequence  */

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

bool debug_allocations = TRUE; //FALSE;

typedef struct
{
	unsigned long cnt;
} ref_counted_base_t, *ref_counted_base_p;

void ref_counted_base_inc(void *data) { ((ref_counted_base_p)data)->cnt++; }
void ref_counted_base_dec(void *data) { if (--((ref_counted_base_p)data)->cnt == 0) { if (debug_allocations) fprintf(stderr, "Free %p\n", data); FREE(data);} }

void result_assign_ref_counted(result_p result, void *data)
{
	if (debug_allocations) fprintf(stderr, "Allocated %p\n", data);
	((ref_counted_base_p)data)->cnt = 1;
	result->data = data;
	result->inc = ref_counted_base_inc;
	result->dec = ref_counted_base_dec;
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

void number_print(void *data, FILE *fout) { fprintf(fout, "number %ld", ((number_data_p)data)->num); }

void new_number_data(result_p result)
{
	result_assign_ref_counted(result, MALLOC(struct number_data));
	result->print = number_print;
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
	Storing the input text
	~~~~~~~~~~~~~~~~~~~~~~
	
	We presume that the parsing algorithm as full access to the input text.
	To implement this the input text is strored in to string. We also want
	to keep track of positions in this input text in terms of lines and
	columns.
*/

struct text_pos
{
	longword pos;     /* Positive offset from the start of the file */
	word cur_line;    /* Line number (1-based) with the position */
	word cur_column;  /* Column number (1-based) with the position */
};

typedef struct
{
	const char *buffer;   /* String containting the input text */
	longword buffer_len;  /* Length of the input text */
	text_pos_t pos;       /* Current position in the input text */
	const char *info;     /* Contents starting at the current position */
	word tab_size;        /* Tabs are on multiples of the tab_size */
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
			  text_buffer->pos.cur_column = ((text_buffer->pos.cur_column + text_buffer->tab_size) % text_buffer->tab_size) * text_buffer->tab_size;
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
	
	Traditional recursive-descent parser are slow, because of the large amount
	of back-tracking occurs, where depending on the properties of the grammar,
	the same fragment of the input is parsed over-and-over again. To prevent
	this, the following section of code, implements a cache for all intermediate
	parsing states (including the results that they have produced).
	
	It stores for each position of the input string the result of parsing a
	certain non-terminal at that point of the input.
*/

enum success_t { s_unknown, s_fail, s_success } ;

typedef struct solution_t solution_t, *solution_p;

struct solution_t 
{
	const char *nt;          /* The name of the non-terminal */
	enum success_t success;  /* Could said non-terminal be parsed from position */
	result_t result;         /* If so, what result did it produce */
	text_pos_t sp;           /* The position (with line and column numbers) */
	solution_p next;         /* Next solution at this location */
};

typedef struct
{
	solution_p *sols;        /* Array of solutions at locations */
	longword len;            /* Length of array (equal to length of input) */
} solutions_t, *solutions_p;

void solutions_init(solutions_p solutions, text_buffer_p text_buffer)
{
    solutions->len = text_buffer->buffer_len;
	solutions->sols = MALLOC_N(solutions->len+1, solution_p);
	longword i;
	for (i = 0; i < solutions->len+1; i++)
		solutions->sols[i] = NULL;
}

void solutions_free(solutions_p solutions)
{
	longword i;
	for (i = 0; i < solutions->len+1; i++)
	{	solution_p sol = solutions->sols[i];

		while (sol != NULL)
		{	if (sol->result.dec != 0)
		    	sol->result.dec(sol->result.data);
			solution_p next_sol = sol->next;
		    FREE(sol);
			sol = next_sol;
		}
  	}
	FREE(solutions->sols);
}

solution_p solutions_find(solutions_p solutions, longword pos, const char *nt)
{
	solution_p sol;

	if (pos > solutions->len)
		pos = solutions->len;

	for (sol = solutions->sols[pos]; sol != NULL; sol = sol->next)
		if (sol->nt == nt)
		 	return sol;

	sol = MALLOC(solution_t);
	sol->next = solutions->sols[pos];
	sol->nt = nt;
	sol->success = s_unknown;
	result_init(&sol->result);
	solutions->sols[pos] = sol;

	return sol;
}


/*
	Recursive decent parser
	~~~~~~~~~~~~~~~~~~~~~~~
*/

int depth = 0;
bool debug_parse = FALSE;
bool debug_nt = FALSE;

#define DEBUG_ENTER(X) if (debug_parse) { DEBUG_TAB; printf("Enter: %s", X); depth += 2; }
#define DEBUG_ENTER_P1(X,A) if (debug_parse) { DEBUG_TAB; printf("Enter: "); printf(X,A); depth += 2; }
#define DEBUG_EXIT(X) if (debug_parse) { depth -=2; DEBUG_TAB; printf("Leave: %s", X); }
#define DEBUG_EXIT_P1(X,A) if (debug_parse) { depth -=2; DEBUG_TAB; printf("Leave: "); printf(X,A); }
#define DEBUG_TAB if (debug_parse) printf("%*.*s", depth, depth, "")
#define DEBUG_NL if (debug_parse) printf("\n")
#define DEBUG_PT(X) if (debug_parse) if (X->print != 0) { X->print(X->data, stdout); } else { printf("<NO_RESULT>"); }
#define DEBUG_PO(X) if (debug_parse) alternative_print(stdout, X)
#define DEBUG_PR(X) if (debug_parse) element_print(stdout, X)
#define DEBUG_(X)  if (debug_parse) printf(X)
#define DEBUG_P1(X,A) if (debug_parse) printf(X,A)

#define DECL_RESULT(V) result_t V; result_init(&V);
#define DISP_RESULT(V) result_release(&V);

typedef struct
{
	text_buffer_p text_buffer;
	solutions_t solutions;
	const char *current_nt;
	element_p current_element;
} parser_t, *parser_p;

void parser_init(parser_p parser, text_buffer_p text_buffer)
{
	parser->text_buffer = text_buffer;
	solutions_init(&parser->solutions, text_buffer);
	parser->current_nt = NULL;
	parser->current_element = NULL;
}

void parser_fini(parser_p parser)
{
	solutions_free(&parser->solutions);
}


bool parse_element(parser_p parser, element_p element, const result_p prev_result, end_function_p end_function, result_p rule_result);
bool parse_seq(parser_p parser, element_p element, const result_p prev_seq, const result_p prev, end_function_p end_function, result_p result);


bool parse_nt(parser_p parser, non_terminal_p non_term, result_p result)
{
	const char *nt = non_term->name;

	DEBUG_ENTER_P1("parse_nt(%s)", nt); DEBUG_NL;

	solution_p sol = solutions_find(&parser->solutions, parser->text_buffer->pos.pos, nt);
	if (sol->success == s_success)
	{
		DEBUG_EXIT_P1("parse_nt(%s) SUCCESS", nt);  DEBUG_NL;
		result_assign(result, &sol->result);
		text_buffer_set_pos(parser->text_buffer, &sol->sp);
		return TRUE;
	}
	else if (sol->success == s_fail)
	{
		DEBUG_EXIT_P1("parse_nt(%s) FAIL", nt);  DEBUG_NL;
		return FALSE;
	}

	const char *surr_nt = parser->current_nt;
	parser->current_nt = nt;

	if (debug_nt)
	{   printf("%*.*s", depth, depth, "");
		printf("Enter: %s\n", nt);
		depth += 2; 
	}

	alternative_p alternative;
	for (alternative = non_term->first; alternative != NULL; alternative = alternative->next )
	{
		DECL_RESULT(start)
		if (parse_element(parser, alternative->rule, &start, alternative->end_function, result))
			break;
	}
	
	if (alternative == NULL)
	{
		DEBUG_EXIT_P1("parse_nt(%s) - failed", nt);  DEBUG_NL;
		if (debug_nt)
		{   depth -= 2;
			printf("%*.*s", depth, depth, "");
			printf("Failed: %s\n", nt);
		}
		parser->current_nt = surr_nt;
		sol->success = s_fail;
		return FALSE;
	}
	
	for(;;)
	{
		for ( alternative = non_term->recursive; alternative != NULL; alternative = alternative->next )
		{
			DECL_RESULT(rule_result)
			if (parse_element(parser, alternative->rule, result, alternative->end_function, &rule_result))
			{   
				result_assign(result, &rule_result);
				DISP_RESULT(rule_result)
				break;
			}
			DISP_RESULT(rule_result)
		}

		if (alternative == NULL)
			break;
	}

	DEBUG_EXIT_P1("parse_nt(%s) = ", nt);
	DEBUG_PT(result); DEBUG_NL;
	if (debug_nt)
	{   depth -= 2;
		printf("%*.*s", depth, depth, "");
		printf("Parsed: %s\n", nt);
	}
	parser->current_nt = surr_nt;
	result_assign(&sol->result, result);
	sol->success = s_success;
	sol->sp = parser->text_buffer->pos;
	return TRUE;
}

bool parse_grouping(parser_p parser, alternative_p alternative, result_p result)
{
	DEBUG_ENTER("parse_grouping: ");
	DEBUG_PO(alternative); DEBUG_NL;

	for ( ; alternative != NULL; alternative = alternative->next )
	{
		DECL_RESULT(start);
		if (parse_element(parser, alternative->rule, &start, alternative->end_function, result))
		{
			DISP_RESULT(start);
			DEBUG_EXIT("parse_grouping = ");
			DEBUG_PT(result); DEBUG_NL;
			return TRUE;
		}
		DISP_RESULT(start);
	}
	DEBUG_EXIT("parse_grouping: failed"); DEBUG_NL;
	return FALSE;
}


bool parse_part(parser_p parser, element_p element, const result_p prev_result, result_p result)
{
	text_pos_t sp = parser->text_buffer->pos;

	bool accepted = FALSE;
	switch( element->kind )
	{
		case rk_nt:
			{
				DECL_RESULT(nt_result)
				if (!parse_nt(parser, element->info.non_terminal, &nt_result))
				{
					DISP_RESULT(nt_result)
					return FALSE;
				}
				
				if (element->condition != 0 && !(*element->condition)(&nt_result, element->condition_argument))
				{
					DISP_RESULT(nt_result)
					text_buffer_set_pos(parser->text_buffer, &sp);
					return FALSE;
				}				
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
				DECL_RESULT(grouping_result);
				if (!parse_grouping(parser, element->info.alternative, &grouping_result))
				{
					DISP_RESULT(grouping_result)
					return FALSE;
				}
				if (element->add_function == 0)
					result_assign(result, prev_result);
				else if (!(*element->add_function)(prev_result, &grouping_result, result))
				{
					DISP_RESULT(grouping_result)
					text_buffer_set_pos(parser->text_buffer, &sp);
					return FALSE;
				}
				DISP_RESULT(grouping_result)
			}
			break;
		case rk_end:
			if (!text_buffer_end(parser->text_buffer))
				return FALSE;
			result_assign(result, prev_result);
			break;
		case rk_char:
			if (*parser->text_buffer->info != element->info.ch)
				return FALSE;
			text_buffer_next(parser->text_buffer);
			if (element->add_char_function == 0)
				result_assign(result, prev_result);
			else if (!(*element->add_char_function)(prev_result, element->info.ch, result))
				return FALSE;
			break;
		case rk_charset:
			if (!char_set_contains(element->info.char_set, *parser->text_buffer->info))
				return FALSE;
			{
				char ch = *parser->text_buffer->info;
				text_buffer_next(parser->text_buffer);
				if (element->add_char_function == 0)
					result_assign(result, prev_result);
				else if (!(*element->add_char_function)(prev_result, ch, result))
					return FALSE;
			}
			break;
		default:
			return FALSE;
			break;
	}
	if (element->set_pos != NULL)
		element->set_pos(result, &sp);

	return TRUE;
}

bool parse_element(parser_p parser, element_p element, const result_p prev_result, end_function_p end_function, result_p rule_result)
{
	DEBUG_ENTER("parse_element: ");
	DEBUG_PR(element); DEBUG_NL;

	/* At the end of the rule: */
	if (element == NULL)
	{   if (end_function != 0)
			end_function(prev_result, rule_result);
		else
			result_assign(rule_result, prev_result);
		DEBUG_EXIT("parse_element = ");
		DEBUG_PT(rule_result); DEBUG_NL;
		return TRUE;
	}

	text_pos_t sp;
	sp = parser->text_buffer->pos;
	

	if (element->optional && element->avoid)
	{
		DECL_RESULT(skip_result);
		if (element->add_skip_function != NULL)
		{
			element->add_skip_function(prev_result, &skip_result);
		}
		else if (element->add_function != NULL)
		{
			DECL_RESULT(empty);
			element->add_function(prev_result, &empty, &skip_result);
			DISP_RESULT(empty);
		}
		else
			result_assign(&skip_result, prev_result);
		if (parse_element(parser, element->next, &skip_result, end_function, rule_result))
		{
			DISP_RESULT(skip_result);
            DEBUG_EXIT("parse_element = ");
            DEBUG_PT(rule_result); DEBUG_NL;
			return TRUE;
		}
		DISP_RESULT(skip_result);
	}
		
	DECL_RESULT(part_result);
	if (element->sequence)
	{
		DECL_RESULT(seq_begin);
		if (element->begin_seq_function != NULL)
			element->begin_seq_function(prev_result, &seq_begin);
		DECL_RESULT(seq_elem);
		if (parse_part(parser, element, &seq_begin, &seq_elem))
		{
			if (parse_seq(parser, element, &seq_elem, prev_result, end_function, rule_result))
			{
				DISP_RESULT(seq_elem);
				DISP_RESULT(seq_begin);
				DEBUG_EXIT("parse_element = ");
				DEBUG_PT(rule_result); DEBUG_NL;
				return TRUE;
			}
		}
		DISP_RESULT(seq_begin);
		DISP_RESULT(seq_elem);
	}
	else
	{
		DECL_RESULT(elem);
		if (parse_part(parser, element, prev_result, &elem))
		{
			if (parse_element(parser, element->next, &elem, end_function, rule_result))
			{
				DISP_RESULT(elem);
				DEBUG_EXIT("parse_element = ");
				DEBUG_PT(rule_result); DEBUG_NL;
				return TRUE;
			}
		}
		DISP_RESULT(elem);
	}
				
	if (element->optional && !element->avoid)
	{
		DECL_RESULT(skip_result);
		if (element->add_skip_function != NULL)
		{
			element->add_skip_function(prev_result, &skip_result);
		}
		else if (element->add_function != NULL)
		{
			DECL_RESULT(empty);
			element->add_function(prev_result, &empty, &skip_result);
			DISP_RESULT(empty);
		}
		else
			result_assign(&skip_result, prev_result);
		if (parse_element(parser, element->next, &skip_result, end_function, rule_result))
		{
			DISP_RESULT(skip_result);
            DEBUG_EXIT("parse_element = ");
            DEBUG_PT(rule_result); DEBUG_NL;
			return TRUE;
		}
		DISP_RESULT(skip_result);
	}

	text_buffer_set_pos(parser->text_buffer, &sp);
	
    DEBUG_EXIT("parse_element: failed"); DEBUG_NL;
	return FALSE;
}

bool parse_seq(parser_p parser, element_p element, const result_p prev_seq, const result_p prev, end_function_p end_function, result_p rule_result)
{
	if (element->avoid)
	{
		DECL_RESULT(result);
		if (element->add_seq_function != NULL)
			element->add_seq_function(prev, prev_seq, &result);
		if (parse_element(parser, element->next, &result, end_function, rule_result))
		{
			DISP_RESULT(result);
			return TRUE;
		}
		DISP_RESULT(result);
	}
	
	if (element->chain_rule != NULL)
	{
		DECL_RESULT(dummy_chain_elem);
		if (!parse_element(parser, element->chain_rule, NULL, NULL, &dummy_chain_elem))
		{
			DISP_RESULT(dummy_chain_elem);
			return FALSE;
		}
		DISP_RESULT(dummy_chain_elem);
	}

	DECL_RESULT(seq_elem);
	if (parse_part(parser, element, prev_seq, &seq_elem))
	{
		if (parse_seq(parser, element, &seq_elem, prev, end_function, rule_result))
		{
			DISP_RESULT(seq_elem);
			DEBUG_EXIT("parse_element = ");
			DEBUG_PT(rule_result); DEBUG_NL;
			return TRUE;
		}
	}
	DISP_RESULT(seq_elem);
	
	if (!element->avoid)
	{
		DECL_RESULT(result);
		if (element->add_seq_function != NULL)
			element->add_seq_function(prev, prev_seq, &result);
		if (parse_element(parser, element->next, &result, end_function, rule_result))
		{
			DISP_RESULT(result);
			return TRUE;
		}
		DISP_RESULT(result);
	}
	
	return FALSE;
}


void test_parse_white_space(non_terminal_p *all_nt, const char *input)
{
	text_buffer_t text_buffer;
	text_buffer_assign_string(&text_buffer, input);
	
	parser_t parser;
	parser_init(&parser, &text_buffer);
	
	DECL_RESULT(result);
	if (parse_nt(&parser, find_nt("white_space", all_nt), &result) && text_buffer_end(&text_buffer))
	{
		fprintf(stderr, "OK: parsed white space\n");
	}
	else
	{
		fprintf(stderr, "ERROR: failed to parse white space from '%s'\n", input);
	}
}

void test_white_space_grammar(non_terminal_p *all_nt)
{
	test_parse_white_space(all_nt, " ");
	test_parse_white_space(all_nt, "/* */");
}

void test_parse_number(non_terminal_p *all_nt, const char *input, int num)
{
	text_buffer_t text_buffer;
	text_buffer_assign_string(&text_buffer, input);
	
	parser_t parser;
	parser_init(&parser, &text_buffer);
	
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

	parser_fini(&parser);
}

void test_number_grammar(non_terminal_p *all_nt)
{
	test_parse_number(all_nt, "0", 0);
	test_parse_number(all_nt, "123", 123);
}

int main(int argc, char *argv[])
{
	non_terminal_p all_nt = NULL;

	// debug_parse = TRUE;
	
	white_space_grammar(&all_nt);
	test_white_space_grammar(&all_nt);

	number_grammar(&all_nt);
	test_number_grammar(&all_nt);

	return 0;
}

#if 0


/*
	A hexadecimal hash tree
	~~~~~~~~~~~~~~~~~~~~~~~	
	The following structure implements a mapping
	of strings to an integer value in the range [0..254].
	It is a tree of hashs in combination with a very
	fast incremental hash function. In this way, it
	tries to combine the benefits of trees and hashs.
	The incremental hash function will first return 
	the lower 4 bits of the characters in the string, 
	and following this the higher 4 bits of the characters.
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

char *string(char *s)
/* Returns a unique address representing the
   string. the global keyword_state will point
   to the integer value in the range [0..254].
   If the string does not occure in the store,
   it is added and the state is initialized with 0.
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
			word i, v = 0;

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
		{   word v;
			if (*vs == '\0')
			{   v = 0;
				if (mode == 0)
				{   mode = 1;
					vs = s;
				}
			}
			else if (mode == 0)
				v = ((word)*vs++) & 15;
			else
				v = ((word)*vs++) >> 4;

			r_node = &node->data.children[v];
		}
	}
}











/*
	Abstract Parse Trees
	~~~~~~~~~~~~~~~~~~~~
	The following section of the code implements
	a representation for Abstract Parse Trees.
*/



typedef struct tree_t tree_t, *tree_p;
typedef struct list_t list_t, *list_p;

struct tree_t
{   char *type;
	union
	{   list_p parts;
		char   *str_value;
		int	int_value;
		double double_value;
		char   char_value;
	} c;
	word line, column;
	longword refcount;
};

struct list_t
{   list_p next;
	tree_p first;
};

char *tt_str_value,
	 *tt_int_value,
	 *tt_list;

/* Special strings, one for each terminal */
char *tt_ident = "identifier";
char *tt_str_value = "string value";
char *tt_int_value = "integer value";
char *tt_double_value = "double value";
char *tt_char_value = "char value";
char *tt_list = "list";


tree_p old_trees = NULL;
list_p old_lists = NULL;
long alloced_trees = 0;

tree_p malloc_tree( void )
{   tree_p new;

	if (old_trees)
	{   new = old_trees;
		old_trees = (tree_p)old_trees->type;
	}
	else
		new = MALLOC(tree_t);

	new->line = 0;
	new->column = 0;
	new->refcount = 1;
	alloced_trees++;

	return new;
} 

void free_list( list_p list );

bool type_can_have_parts( char *type )
{
	return	type != tt_ident 
		   && type != tt_str_value
		   && type != tt_int_value
		   && type != tt_double_value
		   && type != tt_char_value;
}


void tree_release( tree_p *r_t )
{   tree_p t = *r_t;

	if (t == NULL)
		return;

	alloced_trees--;
	t->refcount--;

	if (t->refcount == 0)
	{
		if (type_can_have_parts(t->type))
		{   list_p list = t->c.parts;

			while (list != NULL)
			{   list_p next = list->next;
				free_list(list);
				list = next;
			}
		}

		t->type = (char*)old_trees;
		old_trees = t;
	}
	*r_t = NULL;
}

void free_list( list_p list )
{ tree_release(&list->first);
  list->next = old_lists;
  old_lists = list;
}

void tree_assign(tree_p *d, tree_p s)
{
  tree_p old_d = *d;

  *d = s;
  if (s != NULL)
  {
	s->refcount++;
	alloced_trees++;
  }
  if (old_d != NULL)
	  tree_release(&old_d);
}

void tree_move(tree_p *d, tree_p *s)
{
  tree_p old_d = *d;

  *d = *s;
  *s = NULL;
  if (old_d != NULL)
	  tree_release(&old_d);
}

static list_p malloc_list( void )
{   list_p new;
	if (old_lists)
	{   new = old_lists;
		old_lists = old_lists->next;
	}
	else
		new = MALLOC(list_t);
	new->next = NULL;
	new->first = NULL;
	return new;
} 

tree_p make_ident( char *str )
{
	tree_p tree = malloc_tree();
	tree->type = tt_ident;
	tree->c.str_value = str; 
	return tree;
}

tree_p make_str_atom( char *str )
{
	tree_p tree = malloc_tree();
	tree->type = tt_str_value;
	tree->c.str_value = str; 
	return tree;
}

tree_p make_double_atom( double value )
{
	tree_p tree = malloc_tree();
	tree->type = tt_double_value;
	tree->c.double_value = value; 
	return tree;
}

tree_p make_char_atom( char value )
{
	tree_p tree = malloc_tree();
	tree->type = tt_char_value;
	tree->c.char_value = value; 
	return tree;
}

tree_p make_int_atom( int value )
{
	tree_p tree = malloc_tree();
	tree->type = tt_int_value;
	tree->c.int_value = value; 
	return tree;
}

tree_p make_list( void )
{
	tree_p tree = malloc_tree();
	tree->type = tt_list;
	tree->c.parts = NULL;
	return tree;
}
	
tree_p make_tree( char *name )
{
	tree_p tree = malloc_tree();
	tree->type = name;
	tree->c.parts = NULL;
	return tree;
}
   
void insert_element( tree_p tree, tree_p element )
{
	list_p r_list;

	r_list = malloc_list();
	tree_assign(&r_list->first, element);
	r_list->next = tree->c.parts;
	tree->c.parts = r_list;
} 

void add_element( tree_p tree, tree_p element )
{
	list_p *r_list = &tree->c.parts;

	while (*r_list != NULL)
		r_list = &(*r_list)->next;

	*r_list = malloc_list();
	tree_assign(&(*r_list)->first, element);
}

void drop_last_element( tree_p tree )
{
	list_p *r_list = &tree->c.parts;

	if (*r_list == NULL)
		return;

	while ((*r_list)->next != NULL)
		r_list = &(*r_list)->next;

	free_list(*r_list);
	*r_list = NULL;
} 

inline list_p elements( tree_p tree )
{
	return tree->c.parts;
}

inline bool is_a_ident( tree_p tree )
{
	return tree && tree->type == tt_ident;
}

inline bool is_ident( tree_p tree, char *ident )
{
	return	tree
		   && tree->type == tt_ident
		   && tree->c.str_value == ident;
}

inline bool equal_ident( tree_p tree, char *ident )
{
	return tree->c.str_value == ident;
}

inline bool is_a_str( tree_p tree )
{
	return tree && tree->type == tt_str_value;
}

inline bool is_str( tree_p tree, char *str )
{
	return	tree
		   && tree->type == tt_str_value
		   && tree->c.str_value == str;
}

inline bool is_a_int( tree_p tree )
{
	return tree && tree->type == tt_int_value;
}

inline bool is_a_double( tree_p tree )
{
	return tree && tree->type == tt_double_value;
}

inline bool is_a_char( tree_p tree )
{
	return tree && tree->type == tt_char_value;
}

inline bool is_a_list( tree_p tree )
{
	return tree && tree->type == tt_list;
}

inline bool is_tree( tree_p tree, char *name )
{
	return tree && tree->type == name;
}

inline bool equal_tree( tree_p tree, char *name )
{
	return tree->type == name;
}

int nr_parts( tree_p tree )
{
	list_p parts = tree != NULL ? tree->c.parts : NULL;
	int nr = 0;
	
	for (; parts; parts = parts->next)
		nr++;
		
	return nr;
}	
	
tree_p part( tree_p tree, int i )
{
	list_p parts = tree->c.parts;

	for (i--; parts && i > 0; parts = parts->next, i--);

	return parts ? parts->first : NULL;
}

/*
	Functions for printing the parse tree in a human
	readable form with smart indentation.
*/

int print_tree_depth = 0;
void print_list(FILE *f, list_p list, bool compact);

void print_tree( FILE *f, tree_p tree, bool compact )
{
	if (tree == NULL)
		fprintf(f, "[NULL]");
	else 
	{
		if (tree->line != 0)
			fprintf(f, "#%d:%d#", tree->line, tree->column);
 		if (is_a_ident(tree))
			fprintf(f, "%s", tree->c.str_value);
		else if (is_a_str(tree))
			fprintf(f, "\"%s\"", tree->c.str_value);
		else if (is_a_int(tree))
			fprintf(f, "%d", tree->c.int_value);
		else if (is_a_double(tree))
			fprintf(f, "%f", tree->c.double_value);
		else if (is_a_char(tree))
			fprintf(f, "'%c'", tree->c.char_value);
		else
		{	
			fprintf(f, "%s(", tree->type);
	   
			print_tree_depth += strlen(tree->type) + 1;
			print_list(f, tree->c.parts, compact);
			print_tree_depth -= strlen(tree->type) + 1;
			fprintf(f, ")");
		}
	}
	if (print_tree_depth == 0 && !compact)
		fprintf(f, "\n");
};

void print_list(FILE *f, list_p list, bool compact)
{   list_p l;
	bool first = TRUE;

	for (l = list; l; l = l->next)
	{   if (!first)
		{   if (compact)
				fprintf(f, ",");
			else 
				fprintf(f, ",\n%*s", print_tree_depth, "");
		}
		first = FALSE;
		/* fprintf(f, "[%lx]", (longword)l); */
		print_tree(f, l->first, compact);
	}
}

/*
	Function for printing the Abstract Program Tree
	in the form of a function, which after being
	called, returns the reconstruction of the tree.
	
	This function can be used to hard-code a parser
	for a given grammer. This function was used to
	produce the function init_IParse_grammar below, 
	which creates the Abstract Program Tree representing
	the input grammar of IParse.
*/

static void print_tree_rec( FILE *f, tree_p tree );
 
void print_tree_to_c( FILE *f, tree_p tree, char *name )
{   
	fprintf(f, "void init_%s( tree_p *root )\n", name);
	fprintf(f, "{   /* Generated by IParse version %s */\n", VERSION);
	fprintf(f, "	tree_p tt[100];\n");
	fprintf(f, "	int v = 0;\n");
	fprintf(f, "\n");
	fprintf(f, "#define ID(s) tt[v]=make_ident(string(s));\n");
	fprintf(f, "#define LITERAL(s) tt[v]=make_str_atom(string(s));"
								   " *keyword_state |= 1;\n");
	fprintf(f, "#define INT(i) tt[v]=make_int_atom(i);\n");
	fprintf(f, "#define DOUBLE(i) tt[v]=make_double_atom(i);\n");
	fprintf(f, "#define CHAR(i) tt[v]=make_char_atom(i);\n");
	fprintf(f, "#define TREE(n) tt[v]=make_tree(string(n)); v++;\n");
	fprintf(f, "#define LIST tt[v]=make_list(); v++;\n");
	fprintf(f, "#define NONE tt[v]=NULL;\n");
	fprintf(f, "#define SEP add_element(tt[v-1],tt[v]); tree_release(&tt[v]);\n");
	fprintf(f, "#define CLOSE add_element(tt[v-1],tt[v]); tree_release(&tt[v]); v--;\n");
	fprintf(f, "#define EMPTY_CLOSE v--;\n");
	fprintf(f, "\n	");

	print_tree_rec( f, tree );
	fprintf(f, "\n	*root = tt[0];\n}\n\n");
}

static void print_tree_rec( FILE *f, tree_p tree )
{
	if (tree == NULL)
		 fprintf(f, "NONE ");
	else if (is_a_ident(tree))
	{	fprintf(f, "ID(\"%s\") ", tree->c.str_value);
	}
	else if (is_a_str(tree))
	{	fprintf(f, "LITERAL(\"%s\") ", tree->c.str_value);
	}
	else if (is_a_int(tree))
	{	fprintf(f, "INT(%d) ", tree->c.int_value);
	}
	else if (is_a_double(tree))
	{	fprintf(f, "DOUBLE(%f) ", tree->c.double_value);
	}
	else if (is_a_char(tree))
	{	fprintf(f, "CHAR('%c') ", tree->c.char_value);
	}
	else
	{   list_p l;

		if (is_a_list(tree))
			fprintf(f, "LIST ");
		else
			fprintf(f, "TREE(\"%s\") ", tree->type);
		if (tree->c.parts)
		{   print_tree_depth++;

			for (l = tree->c.parts; l; l = l->next)
			{   print_tree_rec(f, l->first);
				if (l->next)
				  fprintf(f, "SEP\n%*.*s	",
						  print_tree_depth, print_tree_depth, "");
			}
			print_tree_depth--;
			fprintf(f, "CLOSE ");
		}
		else
			fprintf(f, "EMPTY_CLOSE ");
	}
};

int nr_exp_syms = 0;


longword last_space_pos = (longword)-1;
scan_pos_t last_space_end_pos;
longword last_string_pos = (longword)-1;
char *last_string;
scan_pos_t last_string_end_pos;
longword last_ident_pos = (longword)-1;
char *last_ident;
bool last_ident_is_keyword;
scan_pos_t last_ident_end_pos;


void init_scan(FILE *the_file)
{
	_assign(the_file);

	f_file_pos = 0;
	f_line = 1;
	f_column = 1;
	nr_exp_syms = 0;

	last_space_pos = (longword)-1;
	last_string_pos = (longword)-1;
	last_ident_pos = (longword)-1;

}

bool accept_eof()
{
	if (_eof)
	{
		DEBUG_P3("%d.%d: accept_eof() for: `%s'\n",
			 cur_line, start_column, start_info());
		return TRUE;
	}
	else
	{
		DEBUG_F_P3("%d.%d: accept_eof() failed for: `%s'\n",
			 cur_line, cur_column, start_info());
		expected_string("<eof>", NULL);
		return FALSE;
	}
}





char *str_root,
	 *str_nt_def,
	 *str_ident,
	 *str_rule,
	 *str_opt,
	 *str_seq,
	 *str_list,
	 *str_chain,
	 *str_prim_elem,
	 *str_string,
	 *str_cstring,
	 *str_int,
	 *str_double,
	 *str_char,
	 *str_eof;



/*
   Result
   ~~~~~~
   
*/

typedef struct result_t result_t, *result_p
struct result_t {
	// Usually contains a union of possible values
  
	// It is reference counted
	longword ref_count;
};

void free_result(result_p *result)
{
	// Your code for freeing
	
	result = NULL;
}

/*
	Parsing procedures
	~~~~~~~~~~~~~~~~~~
	The following section of the code contains the
	procedures which implement the back-tracking 
	recursive descended parser.
*/

int grammar_level = 2;
char keyword_flag = 1,
	 new_keyword_flag = 2;

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
  char *sym;
  char *in_nt;
  element_p rule;
  char *is_keyword;
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
	expect[nr_exp_syms].rule = current_rule;
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
			if (expect[i].rule == expect[j].rule)
				unique = FALSE;
		if (unique)
		{
			printf("	");
			if (expect[i].rule)
				print_rule(stdout, expect[i].rule);
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



/*
	Symbol tables
	~~~~~~~~~~~~~
	The following section of code deals with the storage of
	symbol tables, and resolving all identifiers with their
	definitions.
*/

typedef struct ident_def_t ident_def_t, *ident_def_p;
typedef struct context_entry_t context_entry_t, *context_entry_p;

struct ident_def_t
{	ident_def_p next;
	tree_p tree;
};

struct context_entry_t
{	context_entry_p next;
	char *ident_name;
	char *ident_class;
	ident_def_p defs;
};


		

/*
	Initialization
	~~~~~~~~~~~~~~
*/

void initialize()
{
	/* init strings */
	str_root = string("root");
	str_nt_def = string("nt_def");
	str_ident = string("ident");
	str_identalone = string("identalone");
	str_identdef = string("identdef");
	str_identdefadd = string("identdefadd");
	str_identuse = string("identuse");
	str_identfield = string("identfield");
	str_rule = string("rule");
	str_opt = string("opt");
	str_seq = string("seq");
	str_list = string("list");
	str_chain = string("chain");
	str_prim_elem = string("prim_elem");
	str_string = string("string");
	str_cstring = string("cstring");
	str_int = string("int");
	str_double = string("double");
	str_char = string("char");
	str_eof = string("eof");

	/* init terminal types */
	add_literal_terminal(str_string, accept_string);
	add_string_terminal(str_cstring, accept_cstring);
	add_int_terminal(str_int, accept_int);
	add_double_terminal(str_double, accept_double);
	add_ident_terminal(str_ident, accept_ident);
	add_char_terminal(str_char, accept_char);

	/* for Transact-SQL: */
	add_string_terminal(string("sql_string_single"), accept_single_sql_string);
	add_string_terminal(string("sql_string_double"), accept_double_sql_string);
	add_ident_terminal(string("sql_ident"), accept_sql_ident);	
	add_ident_terminal(string("sql_vident"), accept_sql_var_ident);	
	add_ident_terminal(string("sql_sysident"), accept_sql_sysvar_ident);	
	add_ident_terminal(string("sql_label"), accept_sql_label_ident);	
}

/*
	Command line interface
	~~~~~~~~~~~~~~~~~~~~~~
	The main procedure implements the command-line interface for IParse.
*/

int main(int argc, char *argv[])
{   tree_p tree = NULL;
	int i;

	if (argc == 1)
	{   printf("Usage: %s <grammar-file> <input-file>\n"
			   "\n"
			   "  options\n"
			   "   -p		print parse tree\n"
			   "   -o <fn>   ouput tree to C file\n"
			   "   +ds	   debug scanning (full)\n"
			   "   +dss	  debug scanning (normal)\n"
			   "   -ds	   no debug scanning\n"
			   "   +dp	   debug parsing\n"
			   "   -dp	   no debug parsing\n"
			   "   +dn	   debug non-terminals\n"
			   "   -dn	   no debug non-terminals\n", argv[0]);
		return 0;
	}

	fprintf(stderr, "Iparser, Version: %s\n", VERSION);

	initialize();
	
	init_IParse_grammar(&tree);

	for (i = 1; i < argc; i++)
	{   char *arg = argv[i];
 
		printf("Processing: %s\n", arg); 

		if (!strcmp(arg, "+g"))
			grammar_level++;
		if (!strcmp(arg, "-p"))
		{   
			printf("tree:\n");
			print_tree(stdout, tree, FALSE);
			printf("\n--------------\n");
		}
		else if (!strcmp(arg, "-pc"))
		{   
			printf("tree:\n");
			print_tree(stdout, tree, TRUE);
			printf("\n--------------\n");
		}
		else if (!strcmp(arg, "-o") && i + 1 < argc)
		{
			char *file_name = argv[++i];
			FILE *fout = !strcmp(file_name, "-") 
						 ? stdout : fopen(file_name, "w");

			if (fout != NULL)
			{   char *dot = strstr(file_name, ".");
				if (dot)
					*dot = '\0';

				print_tree_to_c(fout, tree, file_name);
				fclose(fout);
			}
			else
			{   printf("Cannot open: %s\n", file_name);
				return 0;
			}
		}
		else if (!strcmp(arg, "+ds"))
			debug_scan = 2;
		else if (!strcmp(arg, "+dss"))
			debug_scan = 1;
		else if (!strcmp(arg, "-ds"))
			debug_scan = 0;
		else if (!strcmp(arg, "+dp"))
			debug_parse = TRUE;
		else if (!strcmp(arg, "-dp"))
			debug_parse = FALSE;
		else if (!strcmp(arg, "+dn"))
			debug_nt = TRUE;
		else if (!strcmp(arg, "-dn"))
			debug_nt = FALSE;
		else
		{   FILE *fin = fopen(arg, "r");

			if (fin != NULL)
			{   tree_p new_tree = NULL;

				init_scan(fin);

				make_all_nt(tree);
				tree_release(&tree);
			   
				init_solutions();

				if (!parse_nt(find_nt(str_root), &new_tree))
				{   print_last_pos();
					return 0;
				}
			   	tree_assign(&tree, new_tree);
			   	tree_release(&new_tree);

				free_solutions();

				keyword_flag *= 2;
				new_keyword_flag *= 2;

				grammar_level--;

				fclose(fin);
			}
			else
			{   printf("Cannot open: %s\n", arg);
				return 0;
			}
		}
	}
	tree_release(&tree);
	if (alloced_trees != 0)
		printf("Error: alloced trees = %ld\n", alloced_trees);
	return 0;
} 

/*
	Building a hard-code parser
	~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	IParse implements an interpretting parser. It can also
	be used to implement a parser for a fixed grammar. (Actually,
	that is the way how the parser of the input grammar is
	implemented.)
	
	In case your languages uses different lexical rules for
	the terminals, you will have to write your own scanning
	routines for all these terminals. For this you will have
	to modify this file. Read the sections on scanning for
	information how to do this.
	
	The next step for building a parser consist of writing
	the grammar, for example the file 'lang.gr'. To verify this
	grammar on some input file, for example 'test.lang' call IParse
	from the command line in the following manner:
	
		IParse lang.gr test.gr -p
		
	Once the grammar is correct, call IParse from the command line
	in the following manner:
	
		IParse lang.gr -o lang_grammar.c
		
	This will produce the procedure init_lang_grammar in the file
	lang_grammar.c. Now make a copy of this file and rename it to
	'LangParse.c'. In this copy replace init_IParse_grammar with 
	the code of init_lang_grammar. After 'LangParse.c' has been
	compiled, you can verify its working by calling it from the
	command line with:
	
		LangParse test.gr -p
		
	As a final step, the main procedure can be replaced by a
	procedure which parses a given input file to an Abstract Program
	Tree. An example of such a procedure is:
	
	bool parse_lang(FILE *fin, tree_p *r_tree)
	{   tree_p lang_grammar_tree = NULL;
		bool okay;

		initialize();
		
		init_lang_grammar(&lang_grammar_tree);

		make_all_nt(lang_grammar_tree);
		tree_release(&lang_grammar_tree);
			   
		init_solutions();

		init_scan(fin);

		okay = parse_nt(find_nt(str_root), r_tree);
		
		free_solutions();
		fclose(fin);
		
		if (!okay)		
		{   print_last_pos();
			return FALSE;
		}

		fclose(fin);

		return TRUE;
	}

	This gives you a procedure which will parse a given file
	according to your grammar 'lang.gr' into an Abstract Program Tree,
	in which all identifiers have been resolved. Errors are
	reported on the standard output. If you want them to be
	reported differently, replace the procedure print_last_pos
	by something more fitting.
	If you would like to parse from a string buffer instead of
	from a file, you can replace init_scan by your own initialization
	routine.
*/


#if NEW

void out(Grammar *grammar)
{
#define NT_DEF(name) nt = Grammar_addNonTerminal(grammar, Ident(name)); ref_or_rule = &nt->first; ref_rec_or_rule = &nt->recursive;
#define NEW_GR(K) rule = *ref_rule = new GrammarRule(); ref_rule = &rule->next; rule->kind = K;
#define NT(name) NEW_GR(RK_NT) rule->text.non_terminal = Grammar_addNonTerminal(grammar, Ident(name));
#define TERM(name) NEW_GR(RK_TERM) rule->text.terminal = new GrammarTerminal(Ident(name));
#define LLIT(sym) NEW_GR(RK_LIT) rule->str_value = sym;
#define LIT(sym) LLIT(sym) Grammar_addLiteral(grammar, Ident(sym));
#define WS(name) NEW_GR(RK_WS_TERM) rule->text.terminal = new GrammarTerminal(Ident(name));
#define G_EOF NEW_GR(RK_T_EOF)
#define SEQ rule->sequence = true;
#define CHAIN(sym) SEQ rule->chain_symbol = sym;
#define OPT rule->optional = true;
#define AVOID rule->avoid = true;
#define NONGREEDY rule->nongreedy = true;
#define OPEN NEW_GR(RK_OR_RULE) rule->text.or_rules = new GrammarOrRules; { GrammarOrRule** ref_or_rule = &rule->text.or_rules->first; GrammarOrRule* or_rule; GrammarRule** ref_rule; GrammarRule* rule;
#define OPEN_C NEW_GR(RK_COR_RULE) rule->text.or_rules = new GrammarOrRules; { GrammarOrRule** ref_or_rule = &rule->text.or_rules->first; GrammarOrRule* or_rule; GrammarRule** ref_rule; GrammarRule* rule;
#define OR or_rule = *ref_or_rule = new GrammarOrRule; ref_or_rule = &or_rule->next; ref_rule = &or_rule->rule;
#define CLOSE }
#define REC_OR or_rule = *ref_rec_or_rule = new GrammarOrRule; ref_rec_or_rule = &or_rule->next; ref_rule = &or_rule->rule;
#define TREE(name) or_rule->tree_name = Ident(name);

	GrammarNonTerminal* nt;
	GrammarOrRule** ref_or_rule;
	GrammarOrRule** ref_rec_or_rule;
	GrammarOrRule* or_rule;
	GrammarRule** ref_rule;
	GrammarRule* rule;

	static CharSet all_char;
	CharSet_addRange(all_char, ' ', 126);
	CharSet_addChar(all_char, '\t');
	CharSet_addChar(all_char, '\n');
	static CharSet all_char_except_lt = all_char;
	CharSet_removeChar(all_char, '<');
	
	SET_DEFAULT_WS(0)
	
	NT_DEF("TEXT")
	OR BEGIN(create_string_collector)
	SET_DEFAULT_BEGIN(clone_string_collector_from_parent)
	SET_DEFAULT_END(assign_string_collector_to_parent)
		OPEN
			OR CHARSET(all_char_except_lt, string_append)
			OR CHAR('<', string_append) CHARSET(all_char_except_lt, string_append)
			OR CHAR('\\', 0) CHARSET(all_char, string_append)
		CLOSE SEQ OPT END(finish_string_collector)
	RESET_DEFAULT_BEGIN
	RESET_DEFAULT_END 
	
	NT_DEF("root")
	OR 
	OPEN
		OR NT("TEXT")
		OR LIT("<<")
	CLOSE SEQ OPT
	EOF
}
{
	NT_DEF("primary_expr")
	OR TERM("ident")
	OR TERM("int")
	OR TERM("double")
	OR TERM("char")
	OR TERM("string")
	OR LIT("(") NT("expr") LIT(")")

	NT_DEF("postfix_expr")
	OR NT("primary_expr")
	REC_OR LIT("[") NT("expr") LIT("]") TREE("arrayexp")
	REC_OR LIT("(") NT("assignment_expr") CHAIN(",") OPT LIT(")") TREE("call")
	REC_OR LIT(".") TERM("ident") TREE("field")
	REC_OR LIT("->") TERM("ident") TREE("fieldderef")
	REC_OR LIT("++") TREE("post_inc")
	REC_OR LIT("--") TREE("post_dec")

	NT_DEF("unary_expr")
	OR LIT("++") NT("unary_expr") TREE("pre_inc")
	OR LIT("--") NT("unary_expr") TREE("pre_dec")
	OR LIT("&") NT("cast_expr") TREE("address_of")
	OR LIT("*") NT("cast_expr") TREE("deref")
	OR LIT("+") NT("cast_expr") TREE("plus")
	OR LIT("-") NT("cast_expr") TREE("min")
	OR LIT("~") NT("cast_expr") TREE("invert")
	OR LIT("!") NT("cast_expr") TREE("not")
	OR LIT("sizeof")
	OPEN
		OR NT("unary_expr") TREE("typeof")
		OR LIT("(") TERM("ident") LIT(")")
	CLOSE TREE("sizeof")
	OR NT("postfix_expr")

	NT_DEF("cast_expr")
	OR LIT("(") NT("abstract_declaration") LIT(")") NT("cast_expr") TREE("cast")
	OR NT("unary_expr")

	NT_DEF("l_expr1")
	OR NT("cast_expr")
	REC_OR WS("s") LIT("*") WS("s") NT("cast_expr") TREE("times")
	REC_OR WS("s") LIT("/") WS("s") NT("cast_expr") TREE("div")
	REC_OR WS("s") LIT("%") WS("s") NT("cast_expr") TREE("mod")

	NT_DEF("l_expr2")
	OR NT("l_expr1")
	REC_OR WS("s") LIT("+") WS("s") NT("l_expr1") TREE("add")
	REC_OR WS("s") LIT("-") WS("s") NT("l_expr1") TREE("sub")

	NT_DEF("l_expr3")
	OR NT("l_expr2")
	REC_OR WS("s") LIT("<<") WS("s") NT("l_expr2") TREE("ls")
	REC_OR WS("s") LIT(">>") WS("s") NT("l_expr2") TREE("rs")

	NT_DEF("l_expr4")
	OR NT("l_expr3")
	REC_OR WS("s") LIT("<=") WS("s") NT("l_expr3") TREE("le")
	REC_OR WS("s") LIT(">=") WS("s") NT("l_expr3") TREE("ge")
	REC_OR WS("s") LIT("<") WS("s") NT("l_expr3") TREE("lt")
	REC_OR WS("s") LIT(">") WS("s") NT("l_expr3") TREE("gt")
	REC_OR WS("s") LIT("==") WS("s") NT("l_expr3") TREE("eq")
	REC_OR WS("s") LIT("!=") WS("s") NT("l_expr3") TREE("ne")

	NT_DEF("l_expr5")
	OR NT("l_expr4")
	REC_OR WS("s") LIT("^") WS("s") NT("l_expr4") TREE("bexor")

	NT_DEF("l_expr6")
	OR NT("l_expr5")
	REC_OR WS("s") LIT("&") WS("notamp") WS("s") NT("l_expr5") TREE("land")

	NT_DEF("l_expr7")
	OR NT("l_expr6")
	REC_OR WS("s") LIT("|") WS("s") NT("l_expr6") TREE("lor")

	NT_DEF("l_expr8")
	OR NT("l_expr7")
	REC_OR WS("s") LIT("&&") WS("s") NT("l_expr7") TREE("and")

	NT_DEF("l_expr9")
	OR NT("l_expr8")
	REC_OR WS("s") LIT("||") WS("s") NT("l_expr8") TREE("or")

	NT_DEF("conditional_expr")
	OR NT("l_expr9") WS("s") LIT("?") WS("s") NT("l_expr9") WS("s") LIT(":") WS("s") NT("conditional_expr") TREE("if_expr")
	OR NT("l_expr9")

	NT_DEF("assignment_expr")
	OR NT("unary_expr") WS("s") NT("assignment_operator") WS("s") NT("assignment_expr") TREE("assignment")
	OR NT("conditional_expr")

	NT_DEF("assignment_operator")
	OR LIT("=") TREE("ass")
	OR LIT("*=") TREE("times_ass")
	OR LIT("/=") TREE("div_ass")
	OR LIT("%=") TREE("mod_ass")
	OR LIT("+=") TREE("add_ass")
	OR LIT("-=") TREE("sub_ass")
	OR LIT("<<=") TREE("sl_ass")
	OR LIT(">>=") TREE("sr_ass")
	OR LIT("&=") TREE("and_ass")
	OR LIT("|=") TREE("or_ass")
	OR LIT("^=") TREE("exor_ass")

	NT_DEF("expr")
	OR NT("assignment_expr") CHAIN(",")

	NT_DEF("constant_expr")
	OR NT("conditional_expr")

	NT_DEF("declaration")
	OR WS("nl")
	OPEN
		OR NT("storage_class_specifier")
		OR NT("type_specifier")
	CLOSE SEQ OPT NONGREEDY
	OPEN
		OR
		OPEN
			OR NT("declarator")
			OPEN
				OR WS("s") LIT("=") WS("s") NT("initializer")
			CLOSE OPT
		CLOSE CHAIN(",") OPT LIT(";") TREE("decl")
		OR NT("func_declarator") LIT("(") NT("parameter_declaration_list") OPT LIT(")")
		OPEN
			OR LIT(";")
			OR WS("nl") LIT("{") WS("inc") NT("decl_or_stat") WS("nl") WS("dec") LIT("}")
		CLOSE TREE("new_style")
		OR NT("func_declarator") LIT("(") NT("ident_list") OPT LIT(")") NT("declaration") SEQ OPT WS("nl") LIT("{") WS("inc") NT("decl_or_stat") WS("nl") WS("dec") LIT("}") TREE("old_style")
	CLOSE

	NT_DEF("storage_class_specifier")
	OR LIT("typedef") TREE("typedef")
	OR LIT("extern") TREE("extern")
	OR LIT("static") TREE("static")
	OR LIT("auto") TREE("auto")
	OR LIT("register") TREE("register")

	NT_DEF("type_specifier")
	OR LIT("char") TREE("char")
	OR LIT("short") TREE("short")
	OR LIT("int") TREE("int")
	OR LIT("long") TREE("long")
	OR LIT("signed") TREE("signed")
	OR LIT("unsigned") TREE("unsigned")
	OR LIT("float") TREE("float")
	OR LIT("double") TREE("double")
	OR LIT("const") TREE("const")
	OR LIT("volatile") TREE("volatile")
	OR LIT("void") TREE("void")
	OR NT("struct_or_union_specifier")
	OR NT("enum_specifier")
	OR TERM("ident")

	NT_DEF("struct_or_union_specifier")
	OR LIT("struct") TERM("ident") WS("nl") LIT("{") WS("inc")
	OPEN
		OR WS("nl") NT("struct_declaration")
	CLOSE SEQ WS("dec") WS("nl") LIT("}") TREE("struct_d")
	OR LIT("struct") WS("nl") LIT("{") WS("inc")
	OPEN
		OR WS("nl") NT("struct_declaration")
	CLOSE SEQ WS("dec") WS("nl") LIT("}") TREE("struct_n")
	OR LIT("struct") TERM("ident") TREE("struct")
	OR LIT("union") TERM("ident") WS("nl") LIT("{") WS("inc")
	OPEN
		OR WS("nl") NT("struct_declaration")
	CLOSE SEQ WS("dec") WS("nl") LIT("}") TREE("union_d")
	OR LIT("union") WS("nl") LIT("{") WS("inc")
	OPEN
		OR WS("nl") NT("struct_declaration")
	CLOSE SEQ WS("dec") WS("nl") LIT("}") TREE("union_n")
	OR LIT("union") TERM("ident") TREE("union")

	NT_DEF("struct_declaration")
	OR NT("type_specifier") NT("struct_declaration") TREE("type")
	OR NT("struct_declarator") CHAIN(",") LIT(";") TREE("strdec")

	NT_DEF("struct_declarator")
	OR NT("declarator")
	OPEN
		OR LIT(":") NT("constant_expr")
	CLOSE OPT TREE("record_field")

	NT_DEF("enum_specifier")
	OR LIT("enum") TERM("ident")
	OPEN
		OR LIT("{") NT("enumerator") CHAIN(",") LIT("}")
	CLOSE TREE("enum")

	NT_DEF("enumerator")
	OR TERM("ident")
	OPEN
		OR LIT("=") NT("constant_expr")
	CLOSE OPT TREE("enumerator")

	NT_DEF("func_declarator")
	OR LIT("*")
	OPEN
		OR LIT("const") TREE("const")
	CLOSE OPT NT("func_declarator") TREE("pointdecl")
	OR LIT("(") NT("func_declarator") LIT(")")
	OR TERM("ident")

	NT_DEF("declarator")
	OR LIT("*")
	OPEN
		OR LIT("const") TREE("const")
	CLOSE OPT NT("declarator") TREE("pointdecl")
	OR LIT("(") NT("declarator") LIT(")") TREE("brackets")
	OR WS("s") TERM("ident")
	REC_OR LIT("[") NT("constant_expr") OPT LIT("]") TREE("array")
	REC_OR LIT("(") NT("abstract_declaration_list") OPT LIT(")") TREE("function")

	NT_DEF("abstract_declaration_list")
	OR NT("abstract_declaration")
	OPEN
		OR LIT(",")
		OPEN
			OR LIT("...") TREE("varargs")
			OR NT("abstract_declaration_list")
		CLOSE
	CLOSE OPT

	NT_DEF("parameter_declaration_list")
	OR NT("parameter_declaration")
	OPEN
		OR LIT(",")
		OPEN
			OR LIT("...") TREE("varargs")
			OR NT("parameter_declaration_list")
		CLOSE
	CLOSE OPT

	NT_DEF("ident_list")
	OR TERM("ident")
	OPEN
		OR LIT(",")
		OPEN
			OR LIT("...") TREE("varargs")
			OR NT("ident_list")
		CLOSE
	CLOSE OPT

	NT_DEF("parameter_declaration")
	OR NT("type_specifier") NT("parameter_declaration") TREE("type")
	OR NT("declarator")
	OR NT("abstract_declarator")

	NT_DEF("abstract_declaration")
	OR NT("type_specifier") NT("parameter_declaration") TREE("type")
	OR NT("abstract_declarator")

	NT_DEF("abstract_declarator")
	OR LIT("*")
	OPEN
		OR LIT("const") TREE("const")
	CLOSE OPT NT("abstract_declarator") TREE("abs_pointdecl")
	OR LIT("(") NT("abstract_declarator") LIT(")") TREE("abs_brackets")
	OR
	REC_OR LIT("[") NT("constant_expr") OPT LIT("]") TREE("abs_array")
	REC_OR LIT("(") NT("parameter_declaration_list") LIT(")") TREE("abs_func")

	NT_DEF("initializer")
	OR NT("assignment_expr")
	OR LIT("{") WS("inc") NT("initializer") CHAIN(",") LIT(",") OPT WS("dec") WS("nl") LIT("}") TREE("initializer")

	NT_DEF("decl_or_stat")
	OR NT("declaration") SEQ OPT NT("statement") SEQ OPT

	NT_DEF("statement")
	OR WS("dec") WS("nl")
	OPEN_C
		OR
		OPEN
			OR TERM("ident")
			OR LIT("case") NT("constant_expr")
			OR LIT("default")
		CLOSE LIT(":") WS("inc") NT("statement") TREE("label")
		OR LIT("{") WS("inc") NT("decl_or_stat") WS("dec") WS("nl") LIT("}") WS("inc") TREE("brackets")
	CLOSE
	OR WS("nl")
	OPEN_C
		OR NT("expr") OPT LIT(";")
		OR LIT("if") WS("s") LIT("(") NT("expr") LIT(")") WS("inc") NT("statement")
		OPEN
			OR WS("dec") WS("nl") LIT("else") WS("inc") NT("statement")
		CLOSE OPT WS("dec") TREE("if")
		OR LIT("switch") WS("s") LIT("(") NT("expr") LIT(")") WS("inc") NT("statement") WS("dec") TREE("switch")
		OR LIT("while") WS("s") LIT("(") NT("expr") LIT(")") WS("inc") NT("statement") WS("dec") TREE("while")
		OR LIT("do") WS("inc") NT("statement") WS("dec") WS("nl") LIT("while") WS("s") LIT("(") NT("expr") LIT(")") LIT(";") TREE("do")
		OR LIT("for") WS("s") LIT("(") NT("expr") OPT LIT(";")
		OPEN
			OR WS("s") NT("expr")
		CLOSE OPT LIT(";")
		OPEN
			OR WS("s") NT("expr")
		CLOSE OPT LIT(")") WS("inc") NT("statement") WS("dec") TREE("for")
		OR LIT("goto") TERM("ident") LIT(";") TREE("goto")
		OR LIT("continue") LIT(";") TREE("cont")
		OR LIT("break") LIT(";") TREE("break")
		OR LIT("return") NT("expr") OPT LIT(";") TREE("ret")
	CLOSE

	NT_DEF("root")
	OR
	OPEN
		OR WS("nl") NT("declaration")
	CLOSE SEQ OPT G_EOF
}
#endif

#endif
