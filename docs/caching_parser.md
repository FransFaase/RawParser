# Caching parser

An approach to make the execution of [the simple parser](simple_parser.md)
more efficient, it to implement caching. One this page the implementation
of a caching mechanism for parsed non-terminals is given. Because there
are many possible caching algorithms, a function pointer to a cache
function together with a void pointer to a cache data structure is used.

One approach to improve the performance of a back-tracking recursive-descent
parser (such as [the simple parser](simple_parser.md)) is to use a cache
were intermediate results are stored. One this page the implementation
of a caching mechanism for parsed non-terminals is given. Because there
are various caching strategies an abstract interface for caching is provided.
This abstract interface consists of a function pointer and a void pointer
to a caching data. If the function is not null, it is called at the start
of parsing a non-terminal to query the cache. If a cache item is returned,
it will be stored for the duration of the execution of the function and
thus may not be freed from memory or modified otherwise.

```c
enum success_t { s_unknown, s_fail, s_success } ;

typedef struct cache_item cache_item_t, *cache_item_p;
struct cache_item
{
	enum success_t success;  /* Could said non-terminal be parsed from position */
	text_pos_t next_pos;     /* and from which position (with line and column numbers) should parsing continue */
};

typedef struct cache cache_t, *cache_p;
struct cache
{
	cache_item_p (*get_cache_item)(cache_p cache, size_t start_pos, const char *nt);
	void (*set_result)(cache_item_p cache_item, cache_p cache, size_t start_pos, success_t success, size_t next_pos); 
	void *cache_data;
};

void cache_init(cache_p cache)
{
	cache->get_cache_item = 0;
	cache->set_result = 0;
	cache->cache_data = NULL;
}
```

## Parser struct

In the simple parser, a pointer to the text buffer was passed to the
various parsing functions. Instead of passing also passing a function
pointer to a cache function and a void pointer, it is better to 
introduce a structure representing the parser, which includes all of
these and pass a pointer to this data structure. The following
structure definition and initialization function are defined:
```c
typedef struct parser parser_t, *parser_p;
struct parser
{
	text_buffer_p text_buffer;
	cache_t cache;
};

void parser_init(parser_p parser, text_buffer_p text_buffer)
{
	parser->text_buffer = text_buffer;
	cache_init(&parser->cache);
}
```

## Parse a non-terminal

Below the function is given for parsing a non-terminal. If the function
returns `TRUE` the current location could be advanced beyond the part that was
parsed by the function, otherwise the current location is not affected.
It makes use of the function `parse_rule`, which sees if a productionrule can
be parsed at the current location in the text buffer and is defined in the next section.

```c
bool parse_nt(parser_p parser, non_terminal_p non_term)
{
	cache_item_p cache_item = NULL;
	if (parser->cache.get_cache_item != NULL)
	{
		cache_item = parser->cache.get_cache_item(&parser->cache, parser->text_buffer->pos.pos, non_term);
		if (cache_item != NULL)
		{
			if (cache_item->success == s_success)
			{
				text_buffer_set_pos(parser->text_buffer, &cache_item->next_pos);
				return TRUE;
			}
			else if (cache_item->success == s_fail)
			{
				return FALSE;
			}
			cache_item->success = s_fail; // To deal with indirect left-recurssion
		}
	}


	bool parsed_a_rule = FALSE;
	for (rules_p rule = non_term->normal; rule != NULL; rule = rule->next)
	{
		if (parse_rule(parser->text_buffer, rule->elements))
		{
			parsed_a_rule = TRUE;
			break;
		}
	}
	
	if (!parsed_a_rule)
	{
		if (parser->cache.set_result != 0)
			parser->cache.set_result(cache_item_p, &parser->cache, start_pos, s_fail, 0);
		return FALSE;
	}
	
	while (parsed_a_rule)
	{
		parsed_a_rule = FALSE;
		for (rules_p rule = non_term->recursive; rule != NULL; rule = rule->next)
		{
			if (parse_rule(parser->text_buffer, rule->elements))
			{
				parsed_a_rule = TRUE;
				break;
			}
		}
	}

	if (parser->cache.set_result != 0)
		parser->cache.set_result(cache_item_p, &parser->cache, start_pos, s_success, parser->text_buffer->pos);

	return TRUE;
}
```

## Parse a rule


```c
bool parse_rule(parser_p parser, element_p element)
{
	if (element == NULL)
		return TRUE;

	if (element->optional && element->avoid)
	{
		if (parse_rule(parser->text_buffer, element->next))
			return TRUE;
	}
		
	text_pos_t sp = parser->text_buffer->pos;
	
	if (parse_element(parser->text_buffer, element))
	{
		if (element->sequence)
		{
			if (parse_seq(parser->text_buffer, element))
				return TRUE;
		}
		else
		{
			if (parse_rule(parser->text_buffer, element->next))
				return TRUE;
		}
	}
	
	text_buffer_set_pos(parser->text_buffer, &sp);
	
	if (element->optional && !element->avoid)
	{
		if (parse_rule(parser->text_buffer, element->next))
			return TRUE;
	}

	return FALSE;
}
```

## Parse a sequence

```c
bool parse_seq(parser_p parser, element_p element)
{
	if (element->avoid)
	{
		if (parse_rule(parser->text_buffer, element->next))
			return TRUE;
	}
	
	text_pos_t sp = parser->text_buffer->pos;
	
	if (element->chain_rule == NULL || parse_rule(parser->text_buffer, element->chain_rule))
	{
		
		/* Try to parse the next element of the sequence */
		if (parse_element(parser->text_buffer, element))
		{
			/* If succesful, try to parse the remainder of the sequence (and thereafter the remainder of the rule) */
			if (parse_seq(parser->text_buffer, element))
				return TRUE;
		}
	}
	
	text_buffer_set_pos(parser->text_buffer, &sp);

	if (!element->avoid)
	{
		if (parse_rule(parser->text_buffer, element->next))
			return TRUE;
	}
	
	return FALSE;
}
```

## Parse an element of a rule

The function `parse_element` sees if a single instance of the elmeent of a production
rule can be parsed starting at the current location in the text buffer. If the function
returns `TRUE` the current location could be advanced beyond the part that was
parsed by the function, otherwise the current location is not affected.

```c
bool parse_element(parser_p parser, element_p element)
{
	switch (element->kind)
	{
		case rk_end:
			return text_buffer_end(parser->text_buffer);

		case rk_char:
			if (*parser->text_buffer->info == element->info.ch)
			{
				text_buffer_next(parser->text_buffer);
				return TRUE;
			}
			break;

		case rk_charset:
			if (char_set_contains(element->info.char_set, *parser->text_buffer->info))
			{
				text_buffer_next(parser->text_buffer);
				return TRUE;
			}
			break;
		
		case rk_nt:
			return parse_nt(parser, element->info.non_terminal);

		case rk_grouping:
			for (rules_p rule = element->info.rules; rule != NULL; rule = rule->next )
			{
				if (parse_rule(tparser->ext_buffer, rule->elements))
					return TRUE;
			}
			break;
	}
	
	return FALSE;
}
```


