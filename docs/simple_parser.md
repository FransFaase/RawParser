# Simple parser

On this page we give a straightforward implementation of a simple parser for
[the grammar](grammar.md). It is a back-tracking parser on a buffer
containing the text to be parsed. It is not intended as a very efficient
implementation, but as a reference implementation of how the grammar should
be interpretted.

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

Below the function is given for parsing a non-terminal. If the function
returns `TRUE` the current location could be advanced beyond the part that was
parsed by the function, otherwise the current location is not affected.
It makes use of the function `parse_rule`, which sees if a productionrule can
be parsed at the current location in the text buffer and is defined in the next section.

```c
bool parse_nt(text_buffer_p text_buffer, non_terminal_p non_term)
{
```
* First the function tries to parse one of the normal production rule
  in the order they are listed in the grammar.
```c
	/* Try the normal rules in order of declaration */
	bool parsed_a_rule = FALSE;
	for (rules_p rule = non_term->normal; rule != NULL; rule = rule->next)
	{
		if (parse_rule(text_buffer, rule->elements))
		{
			parsed_a_rule = TRUE;
			break;
		}
	}
	
	if (!parsed_a_rule)
		return FALSE;
```
* If one of the normal could be parsed, the function continues to see if some
  left-recursive production rule can be parsed as long as this is the case
```c
	for(;;)
	{
		parsed_a_rule = FALSE;
		for (rules_p rule = non_term->recursive; rule != NULL; rule = rule->next)
		{
			if (parse_rule(text_buffer, rule->elements))
			{
				parsed_a_rule = TRUE;
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

The function `parse_rule` sees if a [production rule](grammar.md#a-production-rule)
can be parsed starting at the current location in the text buffer. If the function
returns `TRUE` the current location could be advanced beyond the part that was
parsed by the function, otherwise the current location is not affected.
The function is called recursively for the element of the production rule.
It makes use of the function `parse_element` to see if the first element of a rule can
be parsed and the function `parse_seq` which deals with the case the the element
can occure [more than once](grammar.md#modifiers) according to the grammar.

```c
bool parse_rule(text_buffer_p text_buffer, element_p element)
{
```
* if `element` is null, it means that all previous elements of the producion rule
  has been parsed, which indicates that the whole production rule is parsed.
```c
	if (element == NULL)
		return TRUE;

```
* If the first element is optional and should be avoided, first an attempt
  will be made to skip the element and parse the remainder of the rule.
  If this is not succesful, an attempt to parse the element is made.
```c
	if (element->optional && element->avoid)
	{
		if (parse_rule(text_buffer, element->next))
			return TRUE;
	}
		
```
* The current position of the text buffer is saved, such that it can be
  restored in case the elements of the production rule cannot be parsed
  from the current position, and back-tracking is required to try another
  alternative.
```c
	text_pos_t sp = text_buffer->pos;
	
```
* Call the `parse_element` function to see if the first element of the production
  rule can be parsed from the current position in the production rule.
  If this is succesful and the element can occur more than once, the
  function `parse_seq` is called, otherwise the function is called
  recursively for the remainder of the elements of the production rule.
```c
	if (parse_element(text_buffer, element))
	{
		if (element->sequence)
		{
			if (parse_seq(text_buffer, element))
				return TRUE;
		}
		else
		{
			if (parse_rule(text_buffer, element->next))
				return TRUE;
		}
	}
	
```
* At this point in the function, an instance of the first element could
  not be parsed from the current position in the text buffer or it could,
  but the remainder of the elements could not be parsed. reset the current
  position to the saved position.
```c
	text_buffer_set_pos(text_buffer, &sp);
	
```
* If the element was optional (and should not be avoided), see if the
  remainder of the element could be parsed from the current position
  in the text buffer.
```c
	if (element->optional && !element->avoid)
	{
		if (parse_rule(text_buffer, element->next))
			return TRUE;
	}

```
* Failed to parse the rule.
```c
	return FALSE;
}
```

## Parse a sequence

The function `parse_seq` sees if a production rule can be parsed starting at
the current location in the text buffer when the first element can occur as
a sequence and an instance of that element has been parsed succesfully. If the function
returns `TRUE` the current location could be advanced beyond the part that was
parsed by the function, otherwise the current location is not affected.
The function calls itself recursively to parse more instances of the first
the element to form a sequence and calls the function `parse_rule` to try to
parse the remainding elments of the production rule. It makes use of the function
`parse_element` to see if the first element of a rule can be parsed repeatedly.
```c
bool parse_seq(text_buffer_p text_buffer, element_p element)
{
```
* If the first element should be avoided, first an attempt will be made to parse
  the remainder of the rule. If this is not succesful, an attempt to parse the element
  is made.
```c
	if (element->avoid)
	{
		if (parse_rule(text_buffer, element->next))
			return TRUE;
	}
	
```
* The current position of the text buffer is saved, such that it can be
  restored in case the elements of the production rule cannot be parsed
  from the current position, and back-tracking is required to try another
  alternative.
```c
	text_pos_t sp = text_buffer->pos;
	
```
* Another instance of the sequence can only be parsed if there was not
  chain rule or otherwise the chain rule could be parsed at the current
  location in the text buffer.
```c
	if (element->chain_rule == NULL || parse_rule(text_buffer, element->chain_rule))
	{
		
		/* Try to parse the next element of the sequence */
		if (parse_element(text_buffer, element))
		{
			/* If succesful, try to parse the remainder of the sequence (and thereafter the remainder of the rule) */
			if (parse_seq(text_buffer, element))
				return TRUE;
		}
	}
	
```
* At this point in the function, a repeated instance of the first element
  could not be parsed from the current position in the text buffer or it
  could, but the remainder of the elements could not be parsed.
  reset the current position to the saved location.
```c
	text_buffer_set_pos(text_buffer, &sp);

```
  If we have not tried to parse the remainder of the elments of the
  production rule yet (because it should be avoided), see if the
  remainder of the element could be parsed from the current position
  in the text buffer.
```c
	if (!element->avoid)
	{
		if (parse_rule(text_buffer, element->next))
			return TRUE;
	}
```
* Failed to parse the rule.
```c
	
	return FALSE;
}
```

## Parse an element of a rule

The function `parse_element` sees if a single instance of the elmeent of a production
rule can be parsed starting at the current location in the text buffer. If the function
returns `TRUE` the current location could be advanced beyond the part that was
parsed by the function, otherwise the current location is not affected.

```c
bool parse_element(text_buffer_p text_buffer, element_p element)
{
	switch (element->kind)
	{
```
* In case the element specifies that the end of the input should be reached,
  return if this is the case
```c
		case rk_end:
			return text_buffer_end(text_buffer);

```
* In case the element specifies that a certain character is to be expected,
  check if this character occurs at the current location in the text buffer.
  If this the case, advance the current location and return `TRUE`.
```c
		case rk_char:
			if (*text_buffer->info == element->info.ch)
			{
				text_buffer_next(text_buffer);
				return TRUE;
			}
			break;

```
* In case the element specifies that a certain character range is to be expected,
  check if this character at the current location in the text buffer is contained
  in the character set. If this the case, advance the current location and return `TRUE`.
```c
		case rk_charset:
			if (char_set_contains(element->info.char_set, *text_buffer->info))
			{
				text_buffer_next(text_buffer);
				return TRUE;
			}
			break;
		
```
* In case the element specifies that a certain non-terminal is to be expected,
  return the result of calling the `parse_nt` function.
```c
		case rk_nt:
			return parse_nt(text_buffer, element->info.non_terminal);

```
* In case the element specifies [a grouping](grammar.md#grouping), see if one of
  the production rules in this grouping can be parsed from the current location
  in the text buffer, by calling the function `parse_rule`. If one succeeds,
  return `TRUE`.
```c
		case rk_grouping:
			for (rules_p rule = element->info.rules; rule != NULL; rule = rule->next )
			{
				if (parse_rule(text_buffer, rule->elements))
					return TRUE;
			}
			break;
	}
	
	return FALSE;
}
```

