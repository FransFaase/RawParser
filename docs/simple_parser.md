# Straightforward parser

On this page we give a straightforward implementation of a parser for
[the grammar](grammar.md). It is a back-tracking parser on a buffer
containing the text to be parsed. 

## Text buffer

For this, we first have to define a text buffer, with a current position.
A position is defined with a zero-based offset from the start of the
input buffer. For error reporting it is nice to have a 1-based line and
column number. So, this added to a position, which results in the following
definition:
```c
typedef struct text_pos text_pos_t, *text_pos_p;
struct text_pos
{
	size_t pos;               /* Positive offset from the start of the file */
	unsigned int cur_line;    /* Line number (1-based) with the position */
	unsigned int cur_column;  /* Column number (1-based) with the position */
};
```

A text buffer simply consist of a pointer to the input text together with
its length. For parsing it also keeps the current position, with a character
pointer into the buffer (in the member `info`) for the current position. To
calculate the column position in presence of tab-characters, the tab size
needs to be known. This results in the following definition for a text buffer: 
```c
typedef struct text_buffer text_buffer_t, *text_buffer_p;
struct text_buffer
{
	const char *buffer;     /* String containting the input text */
	size_t buffer_len;      /* Length of the input text */
	text_pos_t pos;         /* Current position in the input text */
	const char *info;       /* Contents starting at the current position */
	unsigned int tab_size;  /* Tabs are on multiples of the tab_size */
};
```

Some helper functions for working with a text buffer are defined.
There is a function to initialize a text buffer with a nul-terminated string,
a function to move to the next position, a function to check if the current
position is at the end of the input, and a function to set the current
position to a position that has been saved before (which is used to
back-track in case a certain parsing rule failed). The signatures for
these functions are:
```c
void text_buffer_assign_string(text_buffer_p text_buffer, const char* text);
void text_buffer_next(text_buffer_p text_buffer);
bool text_buffer_end(text_buffer_p text_buffer);
void text_buffer_set_pos(text_buffer_p text_file, text_pos_p text_pos);
```

## Parse a non-terminal



```c
bool parse_nt(text_buffer_p text_buffer, non_terminal_p non_term)
{
	/* Try the normal rules in order of declaration */
	bool parsed_a_rule = false;
	for (rules_p rule = non_term->normal; rule != NULL; rule = rule->next)
	{
		if (parse_rule(text_buffer, rule->elements))
		{
			parsed_a_rule = true;
			break;
		}
	}
	
	if (!parsed_a_rule)
		return FALSE;
	
	/* Now that a normal rule was succesfull, repeatingly try left-recursive rules */
	for(;;)
	{
		parsed_a_rule = false;
		for (rules_p rule = non_term->recursive; rule != NULL; rule = rule->next)
		{
			if (parse_rule(text_buffer, rule->elements))
			{
				parsed_a_rule = true;
				break;
			}
		}

		if (!parsed_a_rule)
			break;
	}

	return TRUE;
}
```

## Parse a rule

```c
bool parse_rule(text_buffer_p text_buffer, element_p element)
{
	if (element == NULL)
	{
		/* At the end of the rule: */
		return TRUE;
	}

	/* Store the current position */
	text_pos_t sp = text_buffer->pos;
	
	/* If the first element is optional and should be avoided, first an attempt
	   will be made to skip the element and parse the remainder of the rule */
	if (element->optional && element->avoid)
	{
		if (parse_rule(text_buffer, element->next))
			return TRUE;
	}
		
	if (element->sequence)
	{
		if (parse_part(text_buffer, element))
		{
			/* Now parse the remainder elements of the sequence (and thereafter the remainder of the rule. */
			if (parse_seq(text_buffer, element))
				return TRUE;
		}
	}
	else
	{
		/* The first element is not a sequence: Try to parse the first element */
		if (parse_part(text_buffer, element))
		{
			if (parse_rule(text_buffer, element->next))
				return TRUE;
		}
	}
	
	/* The element was optional (and should not be avoided): Skip the element
	   and try to parse the remainder of the rule */
	if (element->optional && !element->avoid)
	{
		if (parse_rule(text_buffer, element->next))
			return TRUE;
	}

	/* Failed to parse the rule: reset the current position to the saved position. */
	text_buffer_set_pos(text_buffer, &sp);
	
	return FALSE;
}
```

## Parse a part of a rule

```c
bool parse_part(text_buffer_p text_buffer, element_p element)
{
	switch( element->kind )
	{
		case rk_nt:
			return parse_nt(text_buffer, element->info.non_terminal);

		case rk_grouping:
			/* Try all rules in the grouping */
			for (rules_p rule = element->info.rules; rule != NULL; rule = rule->next )
			{
				if (parse_rule(text_buffer, rule->elements))
					return TRUE;
			}
			break;

		case rk_end:
			/* Check if the end of the buffer is reached */
			return text_buffer_end(text_buffer);

		case rk_char:
			/* Check if the specified character is found at the current position in the text buffer */
			if (*text_buffer->info == element->info.ch)
			{
				/* Advance the current position of the text buffer */
				text_buffer_next(text_buffer);
				return TRUE;
			}
			break;

		case rk_charset:
			/* Check if the character at the current position in the text buffer is found in the character set */
			if (char_set_contains(element->info.char_set, *text_buffer->info))
			{
				/* Advance the current position of the text buffer */
				text_buffer_next(text_buffer);
				return TRUE;
			}
			break;
	}
	
	return FALSE;
}
```

## Parse a sequence

```c
bool parse_seq(text_buffer_p text_buffer, element_p element)
{
	/* In case of the avoid modifier, first an attempt is made to parse the
	   remained of the rule */
	if (element->avoid)
	{
		if (parse_rule(text_buffer, element->next))
			return TRUE;
	}
	
	/* Store the current position */
	text_pos_t sp = text_buffer->pos;

	/* If a chain rule is defined, try to parse it.*/
	if (element->chain_rule != NULL && !parse_rule(text_buffer, element->chain_rule))
		return FALSE;
		
	/* Try to parse the next element of the sequence */
	if (parse_part(text_buffer, element))
	{
		/* If succesful, try to parse the remainder of the sequence (and thereafter the remainder of the rule) */
		if (parse_seq(text_buffer, element))
			return TRUE;
	}
	
	/* Failed to parse the next element of the sequence: reset the current position to the saved position. */
	text_buffer_set_pos(text_buffer, &sp);
	
	/* In case of the avoid modifier, an attempt to parse the remained of the
	   rule, was already made. So, only in case of no avoid modifier, attempt
	   to parse the remainder of the rule */
	if (!element->avoid)
	{
		if (parse_rule(text_buffer, element->next))
			return TRUE;
	}
	
	return FALSE;
}
```
